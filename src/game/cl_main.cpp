#include "main.h"
#include "cl_main.h"
#include "game_shared.h"
#include "inputsystem.h"
#include "mapmanager.h"
#include "network/net_main.h"
#include "capnproto/sidury.capnp.h"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>

//
// The Client, always running unless a dedicated server
// 

// 
// IDEA:
// - Steam Leaderboard data:
//   - how many servers you connected to
//   - how many different maps you loaded
//   - how many bytes you've downloaded from servers in total
//   - how many times you've launched the game
//   

LOG_REGISTER_CHANNEL2( Client, LogColor::White );

static Socket_t                   gClientSocket = CH_INVALID_SOCKET;
static ch_sockaddr                gClientAddr;

static EClientState               gClientState = EClientState_Idle;
static CL_ServerData_t            gClientServerData;

// How much time we have until we give up connecting if the server doesn't respond anymore
static double                     gClientConnectTimeout = 0.f;
static float                      gClientTimeout = 0.f;

// Console Commands to send to the server to process, like noclip
static std::vector< std::string > gCommandsToSend;

extern Entity                     gLocalPlayer;

// NEW_CVAR_FLAG( CVARF_CLIENT );

CONVAR( cl_connect_timeout_duration, 30.f, "How long we will wait for the server to send us connection information" );
CONVAR( cl_timeout_duration, 120.f, "How long we will wait for the server to start responding again before disconnecting" );
CONVAR( cl_timeout_threshold, 4.f, "If the server doesn't send anything after this amount of time, show the connection problem message" );

CONVAR( in_forward, 0, CVARF_INPUT );
CONVAR( in_back, 0, CVARF_INPUT );
CONVAR( in_left, 0, CVARF_INPUT );
CONVAR( in_right, 0, CVARF_INPUT );

CONVAR( in_duck, 0, CVARF_INPUT );
CONVAR( in_sprint, 0, CVARF_INPUT );
CONVAR( in_jump, 0, CVARF_INPUT );
CONVAR( in_zoom, 0, CVARF_INPUT );
CONVAR( in_flashlight, 0, CVARF_INPUT );

CONVAR_CMD_EX( cl_username, "greg", CVARF_ARCHIVE, "Your Username" )
{
	if ( cl_username.GetValue().size() <= CH_MAX_USERNAME_LEN )
		return;

	Log_WarnF( gLC_Client, "Username is too long (%zd chars), max is %d chars\n", cl_username.GetValue().size(), CH_MAX_USERNAME_LEN );
	cl_username.SetValue( prevString );
}


CONCMD( connect )
{
	if ( args.empty() )
	{
		Log_Msg( gLC_Client, "Type in an IP address after the word connect\n" );
		return;
	}

	if ( Game_GetCommandSource() == ECommandSource_Server )
	{
		// the server can only tell us to connect to localhost
		if ( args[ 0 ] != "localhost" )
		{
			Log_Msg( "connect called from server?\n" );
			return;
		}
	}

	CL_Connect( args[ 0 ].data() );
}

bool CL_Init()
{
	EntitySystem::CreateClient();

	// Con_QueueCommand( "connect localhost" );

	return true;
}


void CL_Shutdown()
{
	CL_Disconnect();

	EntitySystem::DestroyClient();
}


bool CL_RecvServerInfo()
{
	ChVector< char > data( 8192 );
	int              len = Net_Read( gClientSocket, data.data(), data.size(), &gClientAddr );

	if ( len <= 0 )
	{
		// NOTE: this might get hit, we need some sort of retry thing
		Log_Warn( gLC_Client, "No Server Info\n" );

		if ( gClientConnectTimeout < Game_GetCurTime() )
			return false;

		// keep waiting i guess?
		return true;
	}

	capnp::FlatArrayMessageReader reader( kj::ArrayPtr< const capnp::word >( (const capnp::word*)data.data(), data.size() ) );
	NetMsgServerInfo::Reader      serverInfoMsg = reader.getRoot< NetMsgServerInfo >();

	// hack, should not be part of this server info message, should be a message prior to this
	// will set up later
	if ( serverInfoMsg.getNewPort() != -1 )
	{
		Net_SetSocketPort( gClientAddr, serverInfoMsg.getNewPort() );
	}

	gClientServerData.aName                     = serverInfoMsg.getName();
	gClientServerData.aMapName                  = serverInfoMsg.getMapName();
	gClientState                                = EClientState_Connecting;

	gLocalPlayer                                = serverInfoMsg.getPlayerEntityId();

	return true;
}


void CL_Update( float frameTime )
{
	Game_SetClient( true );
	Game_SetCommandSource( ECommandSource_Server );

	switch ( gClientState )
	{
		default:
		case EClientState_Idle:
			break;

		case EClientState_RecvServerInfo:
		{
			// Recieve Server Info first
			if ( !CL_RecvServerInfo() )
				CL_Disconnect();

			break;
		}

		case EClientState_Connecting:
		{
			// Try to load the map if we aren't hosting the server
			if ( !Game_IsHosting() )
			{
				// Do we have the map?
				if ( !MapManager_FindMap( gClientServerData.aMapName ) )
				{
					// Maybe one day we can download the map from the server
					CL_Disconnect( "Missing Map" );
					break;
				}

				// Load Map (MAKE THIS ASYNC)
				if ( !MapManager_LoadMap( gClientServerData.aMapName ) )
				{
					CL_Disconnect( "Failed to Load Map" );
					break;
				}
			}

			break;
		}

		case EClientState_Connected:
		{
			// Process Stuff from server
			CL_GetServerMessages();

			// CL_ExecServerCommands();

			CL_GameUpdate( frameTime );

			// Send Console Commands
			if ( gCommandsToSend.size() )
			{
				std::string command;

				// Join it all into one string
				for ( auto& cmd : gCommandsToSend )
					command += cmd + ";";
			}

			// Send UserCmd
			CL_CreateUserCmd();
			break;
		}
	}

	Game_SetCommandSource( ECommandSource_Client );
}


void CL_Disconnect( const char* spReason )
{
	if ( gClientSocket != CH_INVALID_SOCKET )
	{
		if ( spReason )
		{
			// Tell the server why we are disconnecting
			// ::capnp::MallocMessageBuilder message;
			// NetMsgClientInfo::Builder     clientInfoBuild = message.initRoot< NetMsgClientInfo >();

		}

		Net_CloseSocket( gClientSocket );
		gClientSocket = CH_INVALID_SOCKET;
	}

	memset( &gClientAddr, 0, sizeof( gClientAddr ) );

	gClientState          = EClientState_Idle;
	gClientConnectTimeout = 0.f;
}


// TODO: should only be platform specific, needs to have sockaddr abstracted
extern void Net_NetadrToSockaddr( const NetAddr_t* spNetAddr, struct sockaddr* spSockAddr );

void CL_Connect( const char* spAddress )
{
	// Make sure we are not connected to a server already
	CL_Disconnect();

	::capnp::MallocMessageBuilder message;
	NetMsgClientInfo::Builder     clientInfoBuild = message.initRoot< NetMsgClientInfo >();

	clientInfoBuild.setName( cl_username.GetValue().data() );

	gClientSocket     = Net_OpenSocket( "0" );
	NetAddr_t netAddr = Net_GetNetAddrFromString( spAddress );

	Net_NetadrToSockaddr( &netAddr, (struct sockaddr*)&gClientAddr );

	int  what  = Net_Connect( gClientSocket, gClientAddr );

	// kj::HandleOutputStream out( (HANDLE)gClientSocket );
	// capnp::writeMessage( out, message );

	auto array = capnp::messageToFlatArray( message );

	int  write = Net_Write( gClientSocket, array.asChars().begin(), array.size() * sizeof( capnp::word ), &gClientAddr );
	// int  write = Net_Write( gClientSocket, cl_username.GetValue().data(), cl_username.GetValue().size() * sizeof( char ), &sockAddr );

	// Continue connecting in CL_Update()
	gClientState          = EClientState_RecvServerInfo;
	gClientConnectTimeout = Game_GetCurTime() + cl_connect_timeout_duration;
}


void CL_GameUpdate( float frameTime )
{
	// Check connection timeout
	if ( cl_timeout_duration - cl_timeout_threshold < gClientTimeout )
	{
		// Show Connection Warning
		// floood console lol
		Log_WarnF( gLC_Client, "CONNECTION PROBLEM - %.3f SECONDS LEFT\n", gClientTimeout );
	}
}


void CL_CreateUserCmd()
{
	UserCmd_t userCmd{};
	userCmd.aAng;

	userCmd.aButtons = 0;

	if ( in_duck )
		userCmd.aButtons |= EBtnInput_Duck;

	else if ( in_sprint )
		userCmd.aButtons |= EBtnInput_Sprint;

	if ( in_forward ) userCmd.aButtons |= EBtnInput_Forward;
	if ( in_back )    userCmd.aButtons |= EBtnInput_Back;
	if ( in_left )    userCmd.aButtons |= EBtnInput_Left;
	if ( in_right )   userCmd.aButtons |= EBtnInput_Right;
	if ( in_jump )    userCmd.aButtons |= EBtnInput_Jump;
	if ( in_zoom )    userCmd.aButtons |= EBtnInput_Zoom;
	
	if ( in_flashlight == IN_CVAR_JUST_PRESSED )
		userCmd.aFlashlight = true;
}


void CL_HandleMsg_ServerInfo( NetMsgServerInfo::Reader& srReader )
{
}


void CL_HandleMsg_EntityList( NetMsgEntityUpdates::Reader& srReader )
{
	for ( const NetMsgEntityUpdate::Reader& entityUpdate : srReader.getUpdateList() )
	{
		Entity entId  = entityUpdate.getId();
		Entity entity = CH_ENT_INVALID;

		if ( entityUpdate.getState() == NetMsgEntityUpdate::EState::CREATED )
		{
			entity = GetEntitySystem()->CreateEntityFromServer( entId );

			if ( entity == CH_ENT_INVALID )
				continue;
		}
		else if ( entityUpdate.getState() == NetMsgEntityUpdate::EState::DESTROYED )
		{
			GetEntitySystem()->DeleteEntity( entId );
			continue;
		}

		for ( const NetMsgEntityUpdate::Component::Reader& componentRead : entityUpdate.getComponents() )
		{
			const char* spComponentName = componentRead.getName().cStr();

			if ( componentRead.getState() == NetMsgEntityUpdate::EState::CREATED )
			{
				// Create the component
				void* componentData = GetEntitySystem()->AddComponent( entity, spComponentName );

				if ( componentData == nullptr )
				{
					Log_ErrorF( "Failed to create component\n" );
				}
			}
		}

		// use component manager data
		gEntComponentRegistry.aComponents;
	}
}


void CL_GetServerMessages()
{
	ChVector< char > data( 8192 );
	int              len = Net_Read( gClientSocket, data.data(), data.size(), &gClientAddr );

	if ( len <= 0 )
	{
		gClientTimeout -= gFrameTime;

		// The server hasn't sent anything in a while, so just disconnect
		if ( gClientTimeout < 0.0 )
			CL_Disconnect();

		return;
	}

	// Reset the connection timer
	gClientTimeout = cl_timeout_duration;

	// Read the message sent from the client
	capnp::FlatArrayMessageReader reader( kj::ArrayPtr< const capnp::word >( (const capnp::word*)data.data(), data.size() ) );
	auto                          serverMsg = reader.getRoot< MsgSrcServer >();

	auto                          msgType   = serverMsg.getType();
	auto                          msgData   = serverMsg.getData();

	capnp::FlatArrayMessageReader dataReader( kj::ArrayPtr< const capnp::word >( (const capnp::word*)msgData.begin(), data.size() ) );

	switch ( msgType )
	{
		// Client is Disconnecting
		case EMsgSrcServer::DISCONNECT:
		{
			CL_Disconnect();
			return;
		}

		case EMsgSrcServer::SERVER_INFO:
		{
			auto msgServerInfo = dataReader.getRoot< NetMsgServerInfo >();
			CL_HandleMsg_ServerInfo( msgServerInfo );
			break;
		}

		case EMsgSrcServer::CON_VAR:
		{
			auto msgConVar = dataReader.getRoot< NetMsgConVar >();
			Game_ExecCommandsSafe( ECommandSource_Server, msgConVar.getCommand().cStr() );
			break;
		}

		case EMsgSrcServer::ENTITY_LIST:
		{
			auto msgUserCmd = dataReader.getRoot< NetMsgEntityUpdates >();
			CL_HandleMsg_EntityList( msgUserCmd );
			break;
		}

		default:
			// TODO: have a message type to string function
			Log_WarnF( gLC_Client, "Unknown Message Type from Server: %s\n", msgType );
			break;
	}
}


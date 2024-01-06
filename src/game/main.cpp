#include "main.h"
#include "core/systemmanager.h"
#include "core/asserts.h"

#include "iinput.h"
#include "igui.h"
#include "render/irender.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"

#include "util.h"
#include "game_physics.h"
#include "player.h"
#include "entity.h"
#include "igraphics.h"
#include "mapmanager.h"
#include "inputsystem.h"
#include "skybox.h"
#include "testing.h"
#include "steam.h"
#include "network/net_main.h"

#include "cl_main.h"
#include "sv_main.h"

#include "tools/tools.h"

#include "rmlui_interface.h"

#include <SDL_system.h>
#include <SDL_hints.h>

#include <algorithm>


IGuiSystem*      gui          = nullptr;
IRender*         render       = nullptr;
IInputSystem*    input        = nullptr;
IAudioSystem*    audio        = nullptr;
IGraphics*       graphics     = nullptr;
ISteamSystem*    steam        = nullptr;

static bool      gPaused      = false;
float            gFrameTime   = 0.f;

// TODO: make gRealTime and gGameTime
// real time is unmodified time since engine launched, and game time is time affected by host_timescale and pausing
double           gCurTime     = 0.0;  // i could make this a size_t, and then just have it be every 1000 is 1 second

Entity           gLocalPlayer = CH_ENT_INVALID;
ViewportCamera_t gView{};

extern bool      gRunning;
extern ConVar    r_nearz, r_farz, r_fov;
extern ConVar    host_timescale;

// blech
static u32       gMainViewportIndex = UINT32_MAX;

CONVAR( phys_friction, 10 );
CONVAR( dbg_global_axis, 1 );
CONVAR( dbg_global_axis_size, 15 );
CONVAR( r_render, 1 );


// CON_COMMAND( pause )
// {
// 	gui->ShowConsole();
// }


bool CvarF_ClientExecuteCallback( ConVarBase* spBase, const std::vector< std::string >& args )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( spBase->aName, args ) )
		return false;

	return true;
}


void Game_Shutdown()
{
	Resource_Shutdown();

	TEST_Shutdown();

	CL_Shutdown();
	SV_Shutdown();

	Tools_Shutdown();
	Skybox_Destroy();
	Phys_Shutdown();
	Net_Shutdown();

	Steam_Shutdown();
}


// disabled cause for some reason, it could jump to the WindowProc function mid frame and call this 
#define CH_LIVE_WINDOW_RESIZE 0


void WindowResizeCallback()
{
#if CH_LIVE_WINDOW_RESIZE
	PROF_SCOPE_NAMED( "WindowResizeCallback" );

	ImGui::NewFrame();
	ImGui_ImplSDL2_NewFrame();
	graphics->NewFrame();

	Game_UpdateProjection();

	graphics->Reset();

	if ( !( SDL_GetWindowFlags( render->GetWindow() ) & SDL_WINDOW_MINIMIZED ) && r_render )
	{
		gui->Update( 0.f );
		graphics->Present();
	}
	else
	{
		ImGui::EndFrame();
	}
#endif
}


bool Game_Init()
{
	// Startup the Game Input System
	Input_Init();

	Steam_Init();

	// Create the Main Viewport - TODO: use this more across the game code
	gMainViewportIndex = graphics->CreateViewport();

	Game_UpdateProjection();

	Sys_SetResizeCallback( WindowResizeCallback );

	srand( ( unsigned int )time( 0 ) );  // setup rand(  )

#if AUDIO_OPENAL
	// hAudioMusic = audio->RegisterChannel( "Music" );
#endif

	Phys_Init();
	Tools_Init();

	// Init Entity Component Registry
	Ent_RegisterBaseComponents();
	PlayerManager::RegisterComponents();
	TEST_Init();

	if ( !Net_Init() )
	{
		Log_Error( "Failed to init networking\n" );
		return false;
	}

	Con_SetCvarFlagCallback( CVARF_CL_EXEC, CvarF_ClientExecuteCallback );

	// TODO: is this actually even needed if we are only connecting to other servers?
	if ( !SV_Init() )
	{
		Log_Error( "Failed to init server\n" );
		return false;
	}

	if ( !CL_Init() )
	{
		Log_Error( "Failed to init client\n" );
		return false;
	}

	Log_Msg( "Game Loaded!\n" );
	return true;
}


void Game_Update( float frameTime )
{
	PROF_SCOPE();

	{
		PROF_SCOPE_NAMED( "Imgui New Frame" );
		ImGui::NewFrame();
		ImGui_ImplSDL2_NewFrame();
	}

	graphics->NewFrame();

	CH_STEAM_CALL( Update( frameTime ) );

	Game_UpdateGame( frameTime );

	// WORLD GLOBAL AXIS
	// if ( dbg_global_axis )
	// {
	// 	graphics->DrawLine( {0, 0, 0}, {dbg_global_axis_size.GetFloat(), 0, 0}, {1, 0, 0});
	// 	graphics->DrawLine( {0, 0, 0}, {0, dbg_global_axis_size.GetFloat(), 0}, {0, 1, 0} );
	// 	graphics->DrawLine( {0, 0, 0}, {0, 0, dbg_global_axis_size.GetFloat()}, {0, 0, 1} );
	// }

	gui->Update( frameTime );

	if ( !( SDL_GetWindowFlags( render->GetWindow() ) & SDL_WINDOW_MINIMIZED ) && r_render )
	{
		Tools_DrawUI();
		graphics->Present();
	}
	else
	{
		PROF_SCOPE_NAMED( "Imgui End Frame" );
		ImGui::EndFrame();
	}

	Con_Update();

	Resource_Update();
}


bool Game_InMap()
{
	return MapManager_HasMap();
}


void Game_UpdateGame( float frameTime )
{
	PROF_SCOPE();

	gFrameTime = frameTime * host_timescale;

	Game_SetClient( true );

	Game_HandleSystemEvents();

	Input_Update();

	if ( gPaused )
	{
		gFrameTime = 0.f;
	}

	gCurTime += gFrameTime;

	Tools_Update( gFrameTime );

	if ( gServerData.aActive )
		SV_Update( gFrameTime );

	// when do i call these lol
	CL_Update( gFrameTime );
}


void Game_SetPaused( bool paused )
{
	gPaused = paused;
}


bool Game_IsPaused()
{
	return gPaused;
}


void Game_HandleSystemEvents()
{
	PROF_SCOPE();

	static std::vector< SDL_Event >* events = input->GetEvents();

	for ( auto& e: *events )
	{
		switch (e.type)
		{
			// TODO: remove this and use pause concmd, can't at the moment as binds are only parsed when game is active, hmm
			case SDL_KEYDOWN:
			{
				if ( e.key.keysym.sym == SDLK_BACKQUOTE || e.key.keysym.sym == SDLK_ESCAPE )
					gui->ShowConsole();
			
				break;
			}

			case SDL_WINDOWEVENT:
			{
				switch (e.window.event)
				{
					case SDL_WINDOWEVENT_DISPLAY_CHANGED:
					{
						// static float prevDPI = 96.f;
						// 
						// float dpi, hdpi, vdpi;
						// int test = SDL_GetDisplayDPI( e.window.data1, &dpi, &hdpi, &vdpi );
						// 
						// ImGuiStyle& style = ImGui::GetStyle();
						// style.ScaleAllSizes( dpi / prevDPI );
						// 
						// prevDPI = dpi;
					}

					case SDL_WINDOWEVENT_SIZE_CHANGED:
					{
						// Log_Msg( "SDL_WINDOWEVENT_SIZE_CHANGED\n" );
						Game_UpdateProjection();
						graphics->Reset();
						break;
					}
					case SDL_WINDOWEVENT_EXPOSED:
					{
						// Log_Msg( "SDL_WINDOWEVENT_EXPOSED\n" );
						// TODO: RESET RENDERER AND DRAW ONTO WINDOW WINDOW !!!
						break;
					}
				}
				break;
			}

			default:
			{
				break;
			}
		}
	}
}


// TODO: this will not work for multiple camera viewports
void Game_SetView( const glm::mat4& srViewMat )
{
	gView.aViewMat = srViewMat;
}


// TODO: this will not work for multiple camera viewports
void Game_UpdateProjection()
{
	PROF_SCOPE();

	int width = 0, height = 0;
	render->GetSurfaceSize( width, height );
	gView.ComputeProjection( width, height );

	ViewportShader_t* viewport = graphics->GetViewportData( gMainViewportIndex );

	if ( !viewport )
		return;

	// um
	viewport->aProjView   = gView.aProjViewMat;
	viewport->aProjection = gView.aProjMat;
	viewport->aView       = gView.aViewMat;
	viewport->aNearZ      = gView.aNearZ;
	viewport->aFarZ       = gView.aFarZ;
	viewport->aSize       = { width, height };

	graphics->SetViewportUpdate( true );

	auto& io = ImGui::GetIO();
	io.DisplaySize.x = width;
	io.DisplaySize.y = height;
}


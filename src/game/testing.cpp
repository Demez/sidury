#include "testing.h"
#include "main.h"
#include "inputsystem.h"
#include "graphics/graphics.h"

#include "cl_main.h"
#include "sv_main.h"

#include "imgui/imgui.h"

// ---------------------------------------------------------------------------
// This file is dedicated for random stuff and experiments with the game
// and will be quite messy


CONVAR( vrcmdl_scale, 40 );
CONVAR( in_proto_spam, 0, CVARF_INPUT );
CONVAR( proto_look, 1 );
CONVAR( proto_follow, 0 );
CONVAR( proto_follow_speed, 400 );
CONVAR( proto_swap_target_sec_min, 1.0 );
CONVAR( proto_swap_target_sec_max, 10.0 );
CONVAR( proto_swap_turn_time_min, 0.2 );
CONVAR( proto_swap_turn_time_max, 3.0 );
CONVAR( proto_look_at_entities, 1 );

extern ConVar                 phys_friction;
extern ConVar                 cl_view_height;

// extern Entity                 gLocalPlayer;

// Audio Channels
Handle                        hAudioMusic = InvalidHandle;

// testing
std::vector< Handle >         streams{};
Model*                        g_streamModel = nullptr;

ConVar                        snd_cube_scale( "snd_cube_scale", "0.05" );


// Was for an old multithreading test
#if 0
struct ProtoLookData_t
{
	std::vector< Entity > aProtos;
	Transform*            aPlayerTransform;
};
#endif


// Protogen Component
// This acts a tag to put all entities here in a list automatically
struct CProtogen
{
};


// funny temp
struct ProtoTurn_t
{
	double aNextTime;
	float  aLastRand;
	float  aRand;
};


// Information for protogen smooth turning to look toward targets
struct ProtoLookData_t
{
	glm::vec3 aStartAng{};
	glm::vec3 aGoalAng{};

	float     aTurnCurTime   = 0.f;
	float     aTurnEndTime   = 0.f;

	float     aTimeToDuel    = 0.f;
	float     aTimeToDuelCur = 0.f;

	Entity    aLookTarget    = CH_ENT_INVALID;
};


#define CH_PROTO_SV 0
#define CH_PROTO_CL 1


class ProtogenSystem : public IEntityComponentSystem
{
  public:
	ProtogenSystem()
	{
	}

	~ProtogenSystem()
	{
	}

	void ComponentAdded( Entity sEntity ) override
	{
		if ( !Game_ProcessingClient() )
			return;

		// Add a renderable component
		auto renderable     = Ent_AddComponent< CRenderable_t >( sEntity, "renderable" );
		renderable->aHandle = InvalidHandle;
	}

	void ComponentRemoved( Entity sEntity ) override
	{
		if ( !Game_ProcessingClient() )
			return;

		auto renderComp = Ent_GetComponent< CRenderable_t >( sEntity, "renderable" );

		if ( !renderComp )
			return;

		Renderable_t* renderable = Graphics_GetRenderableData( renderComp->aHandle );

		if ( renderable )
			Graphics_FreeModel( renderable->aModel );

		Graphics_FreeRenderable( renderComp->aHandle );

		aTurnMap.erase( sEntity );
		aLookMap.erase( sEntity );

		// GetEntitySystem()->RemoveComponent( sEntity, "renderable" );
	}

	void ComponentUpdated( Entity sEntity ) override
	{
		if ( Game_ProcessingClient() )
			aUpdated.push_back( sEntity );
	}

	void Update() override
	{
	}

	std::vector< Entity >                         aUpdated;

	std::unordered_map< Entity, ProtoTurn_t >     aTurnMap;
	std::unordered_map< Entity, ProtoLookData_t > aLookMap;
};


static ProtogenSystem* gProtoSystems[ 2 ] = { 0, 0 };


ProtogenSystem*        GetProtogenSys()
{
	int i = Game_ProcessingClient() ? CH_PROTO_CL : CH_PROTO_SV;
	Assert( gProtoSystems[ i ] );
	return gProtoSystems[ i ];
}

// ===========================================================================


void CreateProtogen_f( const std::string& path )
{
#if 1
	Entity player = SV_GetCommandClientEntity();

	if ( !player )
		return;

	Entity proto = GetEntitySystem()->CreateEntity();

	Ent_AddComponent( proto, "protogen" );

	Handle      model          = Graphics_LoadModel( path );

	CModelInfo* modelInfo      = Ent_AddComponent< CModelInfo >( proto, "modelInfo" );
	modelInfo->aPath           = path;

	// CRenderable_t* renderComp  = Ent_AddComponent< CRenderable_t >( proto, "renderable" );
	// renderComp->aHandle        = Graphics_CreateRenderable( model );

	CTransform* transform       = Ent_AddComponent< CTransform >( proto, "transform" );

	auto       playerTransform = Ent_GetComponent< CTransform >( player, "transform" );

	transform->aPos            = playerTransform->aPos;
	transform->aScale.Set( { vrcmdl_scale.GetFloat(), vrcmdl_scale.GetFloat(), vrcmdl_scale.GetFloat() } );
#endif
}

void CreateModelEntity( const std::string& path )
{
#if 0
	Entity physEnt = GetEntitySystem()->CreateEntity();

	Model* model   = Graphics_LoadModel( path );
	GetEntitySystem()->AddComponent< Model* >( physEnt, model );
	Transform& transform = GetEntitySystem()->AddComponent< Transform >( physEnt );

	Transform& playerTransform = GetEntitySystem()->GetComponent< Transform >( game->aLocalPlayer );
	transform = playerTransform;

	g_staticEnts.push_back( physEnt );
#endif
}

void CreatePhysEntity( const std::string& path )
{
#if 0
	Entity         physEnt    = GetEntitySystem()->CreateEntity();

	Handle         model      = Graphics_LoadModel( path );

	CRenderable_t& renderComp = GetEntitySystem()->AddComponent< CRenderable_t >( physEnt );
	renderComp.aHandle        = Graphics_CreateRenderable( model );

	PhysicsShapeInfo shapeInfo( PhysShapeType::Convex );

	Phys_GetModelVerts( model, shapeInfo.aConvexData );

	IPhysicsShape* shape = physenv->CreateShape( shapeInfo );

	if ( shape == nullptr )
	{
		Log_ErrorF( "Failed to create physics shape for model: \"%s\"\n", path.c_str() );
		return;
	}

	Transform&        transform = GetEntitySystem()->GetComponent< Transform >( gLocalPlayer );

	PhysicsObjectInfo physInfo;
	physInfo.aPos         = transform.aPos;  // NOTE: THIS IS THE CENTER OF MASS
	physInfo.aAng         = transform.aAng;
	physInfo.aMass        = 1.f;
	physInfo.aMotionType  = PhysMotionType::Dynamic;
	physInfo.aStartActive = true;

	IPhysicsObject* phys  = physenv->CreateObject( shape, physInfo );
	phys->SetFriction( phys_friction );

	Phys_SetMaxVelocities( phys );

	GetEntitySystem()->AddComponent< IPhysicsShape* >( physEnt, shape );
	GetEntitySystem()->AddComponent< IPhysicsObject* >( physEnt, phys );

	g_otherEnts.push_back( physEnt );
#endif
}


CONCMD_VA( create_proto, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_proto" ) )
		return;

	CreateProtogen_f( DEFAULT_PROTOGEN_PATH );
}


CONCMD_VA( create_gltf_proto, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_gltf_proto" ) )
		return;

	CreateProtogen_f( "materials/models/protogen_wip_25d/protogen_25d.glb" );
}


static void model_dropdown(
  const std::vector< std::string >& args,  // arguments currently typed in by the user
  std::vector< std::string >&       results )    // results to populate the dropdown list with
{
	for ( const auto& file : FileSys_ScanDir( "materials", ReadDir_AllPaths | ReadDir_NoDirs | ReadDir_Recursive ) )
	{
		if ( file.ends_with( ".." ) )
			continue;

		if ( args.size() && !file.starts_with( args[ 0 ] ) )
			continue;

		// make sure it's a format we can open
		if ( file.ends_with( ".obj" ) || file.ends_with( ".glb" ) || file.ends_with( ".gltf" ) )
			results.push_back( file );
	}
}


CONCMD_DROP_VA( create_look_entity, model_dropdown, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_look_entity", args ) )
		return;

	if ( args.size() )
		CreateProtogen_f( args[ 0 ] );
}


CONCMD_VA( delete_protos, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "delete_protos" ) )
		return;

	// for ( auto& proto : GetProtogenSys()->aEntities )
	while ( GetProtogenSys()->aEntities.size() )
	{
		// Graphics_FreeModel( GetEntitySystem()->GetComponent< Model* >( proto ) );
		// Graphics_FreeRenderable( GetEntitySystem()->GetComponent< CRenderable_t >( proto ).aHandle );
		GetEntitySystem()->DeleteEntity( GetProtogenSys()->aEntities[ 0 ] );
	}
}


#if 0
CONCMD_VA( create_phys_test, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_phys_test" ) )
		return;

	CreatePhysEntity( "materials/models/riverhouse/riverhouse.obj" );
}


CONCMD_VA( create_phys_proto, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_phys_proto") )
		return;

	CreatePhysEntity( "materials/models/protogen_wip_25d/protogen_wip_25d_big.obj" );
}


CONCMD_VA( create_phys_ent, CVARF( CL_EXEC ) )
{
	// Forward to server if we are the client
	if ( CL_SendConVarIfClient( "create_phys_ent", args ) )
		return;

	if ( args.size() )
		CreatePhysEntity( args[ 0 ] );
}
#endif


void TEST_Init()
{
	CH_REGISTER_COMPONENT( CProtogen, protogen, false, EEntComponentNetType_Both );
	CH_REGISTER_COMPONENT_SYS( CProtogen, ProtogenSystem, gProtoSystems );
}


void TEST_Shutdown()
{
	// for ( int i = 0; i < g_physEnts.size(); i++ )
	// {
	// 	if ( g_physEnts[ i ] )
	// 		delete g_physEnts[ i ];
	// 
	// 	g_physEnts[ i ] = nullptr;
	// }
}


static ImVec2 ImVec2Mul( ImVec2 vec, float sScale )
{
	vec.x *= sScale;
	vec.y *= sScale;
	return vec;
}

static ImVec2 ImVec2MulMin( ImVec2 vec, float sScale, float sMin = 1.f )
{
	vec.x *= sScale;
	vec.y *= sScale;

	vec.x = glm::max( sMin, vec.x );
	vec.y = glm::max( sMin, vec.y );

	return vec;
}

static void ScaleImGui( float sScale )
{
	static ImGuiStyle baseStyle     = ImGui::GetStyle();

	ImGuiStyle&       style         = ImGui::GetStyle();

	style.ChildRounding             = baseStyle.ChildRounding * sScale;
	style.WindowRounding            = baseStyle.WindowRounding * sScale;
	style.PopupRounding             = baseStyle.PopupRounding * sScale;
	style.FrameRounding             = baseStyle.FrameRounding * sScale;
	style.IndentSpacing             = baseStyle.IndentSpacing * sScale;
	style.ColumnsMinSpacing         = baseStyle.ColumnsMinSpacing * sScale;
	style.ScrollbarSize             = baseStyle.ScrollbarSize * sScale;
	style.ScrollbarRounding         = baseStyle.ScrollbarRounding * sScale;
	style.GrabMinSize               = baseStyle.GrabMinSize * sScale;
	style.GrabRounding              = baseStyle.GrabRounding * sScale;
	style.LogSliderDeadzone         = baseStyle.LogSliderDeadzone * sScale;
	style.TabRounding               = baseStyle.TabRounding * sScale;
	style.MouseCursorScale          = baseStyle.MouseCursorScale * sScale;
	style.TabMinWidthForCloseButton = ( baseStyle.TabMinWidthForCloseButton != FLT_MAX ) ? ( baseStyle.TabMinWidthForCloseButton * sScale ) : FLT_MAX;

	style.WindowPadding             = ImVec2Mul( baseStyle.WindowPadding, sScale );
	style.WindowMinSize             = ImVec2MulMin( baseStyle.WindowMinSize, sScale );
	style.FramePadding              = ImVec2Mul( baseStyle.FramePadding, sScale );
	style.ItemSpacing               = ImVec2Mul( baseStyle.ItemSpacing, sScale );
	style.ItemInnerSpacing          = ImVec2Mul( baseStyle.ItemInnerSpacing, sScale );
	style.CellPadding               = ImVec2Mul( baseStyle.CellPadding, sScale );
	style.TouchExtraPadding         = ImVec2Mul( baseStyle.TouchExtraPadding, sScale );
	style.DisplayWindowPadding      = ImVec2Mul( baseStyle.DisplayWindowPadding, sScale );
	style.DisplaySafeAreaPadding    = ImVec2Mul( baseStyle.DisplaySafeAreaPadding, sScale );
}


void TEST_EntUpdate()
{
#if 0
	PROF_SCOPE();

	// blech
	for ( auto& ent : g_otherEnts )
	{
		CRenderable_t& renderComp = GetEntitySystem()->GetComponent< CRenderable_t >( ent );

		if ( Renderable_t* renderable = Graphics_GetRenderableData( renderComp.aHandle ) )
		{
			if ( renderable->aModel == InvalidHandle )
				continue;

			// Model *physObjList = &GetEntitySystem()->GetComponent< Model >( ent );

			// Transform& transform = GetEntitySystem()->GetComponent< Transform >( ent );
			IPhysicsObject* phys = GetEntitySystem()->GetComponent< IPhysicsObject* >( ent );

			if ( !phys )
				continue;

			phys->SetFriction( phys_friction );
			Util_ToMatrix( renderable->aModelMatrix, phys->GetPos(), phys->GetAng() );
		}
	}

#if 0
	// what
	if ( ImGui::Begin( "What" ) )
	{
		ImGuiStyle& style = ImGui::GetStyle();

		static float imguiScale = 1.f;

		if ( ImGui::SliderFloat( "Scale", &imguiScale, 0.25f, 4.f, "%.4f", 1.f ) )
		{
			ScaleImGui( imguiScale );
		}
	}

	ImGui::End();
#endif
#endif
}


#if 0
void TaskUpdateProtoLook( ftl::TaskScheduler *taskScheduler, void *arg )
{
	float protoScale = vrcmdl_scale;

	ProtoLookData_t* lookData = (ProtoLookData_t*)arg;
	
	// TODO: maybe make this into some kind of "look at player" component? idk lol
	// also could thread this as a test
	for ( auto& proto : lookData->aProtos )
	{
		DefaultRenderable* renderable = (DefaultRenderable*)GetEntitySystem()->GetComponent< RenderableHandle_t >( proto );
		auto& protoTransform = GetEntitySystem()->GetComponent< Transform >( proto );

		bool matrixChanged = false;

		if ( proto_look.GetBool() )
		{
			matrixChanged = true;

			glm::vec3 forward{}, right{}, up{};
			//AngleToVectors( protoTransform.aAng, forward, right, up );
			AngleToVectors( lookData->aPlayerTransform->aAng, forward, right, up );

			glm::vec3 protoView = protoTransform.aPos;
			//protoView.z += cl_view_height;

			glm::vec3 direction = (protoView - lookData->aPlayerTransform->aPos);
			// glm::vec3 rotationAxis = VectorToAngles( direction );
			glm::vec3 rotationAxis = VectorToAngles( direction, up );

			protoTransform.aAng = rotationAxis;
			protoTransform.aAng[PITCH] = 0.f;
			protoTransform.aAng[YAW] -= 90.f;
			protoTransform.aAng[ROLL] = (-rotationAxis[PITCH]) + 90.f;
			//protoTransform.aAng[ROLL] = 90.f;
		}

		if ( protoTransform.aScale.x != protoScale )
		{
			// protoTransform.aScale = {protoScale, protoScale, protoScale};
			protoTransform.aScale = glm::vec3( protoScale );
			matrixChanged = true;
		}

		if ( matrixChanged )
			renderable->aMatrix = protoTransform.ToMatrix();
	}
}
#endif


CONVAR( r_proto_line_dist, 32.f );
CONVAR( r_proto_line_dist2, 32.f );


void TEST_CL_UpdateProtos( float frameTime )
{
	PROF_SCOPE();

	// TEMP
	for ( auto& proto : GetProtogenSys()->aEntities )
	{
		auto modelInfo      = Ent_GetComponent< CModelInfo >( proto, "modelInfo" );
		auto renderComp     = Ent_GetComponent< CRenderable_t >( proto, "renderable" );
		auto protoTransform = Ent_GetComponent< CTransform >( proto, "transform" );

		Assert( modelInfo );
		Assert( renderComp );
		Assert( protoTransform );

		// I have to do this here, and not in ComponentAdded(), because modelPath may not added yet
		if ( renderComp->aHandle == InvalidHandle )
		{
			Handle model = Graphics_LoadModel( modelInfo->aPath );

			if ( !model )
				continue;

			Handle        renderable = Graphics_CreateRenderable( model );
			Renderable_t* renderData = Graphics_GetRenderableData( renderable );

			if ( !renderData )
			{
				Log_Error( "scream\n" );
				continue;
			}

			renderComp->aHandle = renderable;
		}

		Renderable_t* renderData = Graphics_GetRenderableData( renderComp->aHandle );

		if ( renderData )
		{
			Util_ToMatrix( renderData->aModelMatrix, protoTransform->aPos, protoTransform->aAng, protoTransform->aScale );
			Graphics_UpdateRenderableAABB( renderComp->aHandle );
		}

		// glm::vec3 modelForward, modelRight, modelUp;
		// Util_GetMatrixDirection( protoTransform->ToMatrix(), &modelForward, &modelRight, &modelUp );
		// Graphics_DrawLine( protoTransform->aPos, protoTransform->aPos + ( modelForward * r_proto_line_dist.GetFloat() ), { 1.f, 0.f, 0.f } );
		// Graphics_DrawLine( protoTransform->aPos, protoTransform->aPos + ( modelRight * r_proto_line_dist.GetFloat() ), { 0.f, 1.f, 0.f } );
		// Graphics_DrawLine( protoTransform->aPos, protoTransform->aPos + ( modelUp * r_proto_line_dist.GetFloat() ), { 0.f, 0.f, 1.f } );
	}

	GetProtogenSys()->aUpdated.clear();
}


extern float Lerp_GetDuration( float max, float min, float current );
extern float Lerp_GetDuration( float max, float min, float current, float mult );

// inverted version of it
extern float Lerp_GetDurationIn( float max, float min, float current );
extern float Lerp_GetDurationIn( float max, float min, float current, float mult );

extern float Math_EaseOutExpo( float x );
extern float Math_EaseOutQuart( float x );
extern float Math_EaseInOutCubic( float x );


void TEST_SV_UpdateProtos( float frameTime )
{
#if 1
	PROF_SCOPE();

	// Protogens have to have look targets, if there are no players, don't bother executing this code
	// As no player will even see the protogens looking around
	if ( gServerData.aClients.empty() )
		return;

	// ?????
	float protoScale = vrcmdl_scale;

	// TODO: maybe make this into some kind of "look at player" component? idk lol
	// also could thread this as a test
	for ( auto& proto : GetProtogenSys()->aEntities )
	{
		ProtoLookData_t& protoLook     = GetProtogenSys()->aLookMap[ proto ];
		bool             targetChanged = false;

		protoLook.aTimeToDuelCur += frameTime;

		if ( protoLook.aTimeToDuelCur > protoLook.aTimeToDuel )
		{
			if ( proto_look_at_entities )
			{
				// Reset it back to 0 to enter the while loop of not picking the world or itself
				protoLook.aLookTarget = 0;

				// Don't look at yourself or the world, only pick the world if there are no other entities to pick
				if ( GetEntitySystem()->aEntityCount > 2 )
				{
					while ( protoLook.aLookTarget == 0 || protoLook.aLookTarget == proto )
						protoLook.aLookTarget = RandomInt( 0, GetEntitySystem()->aEntityCount - 1 );
				}
			}
			else
			{
				size_t playerID       = RandomInt( 0, gServerData.aClients.size() - 1 );
				protoLook.aLookTarget = SV_GetPlayerEntFromIndex( playerID );

				if ( protoLook.aLookTarget == CH_ENT_INVALID )
					continue;
			}

			protoLook.aTimeToDuel    = RandomFloat( proto_swap_target_sec_min, proto_swap_target_sec_max );
			protoLook.aTimeToDuelCur = 0.f;
			targetChanged            = true;
		}

		auto playerTransform = Ent_GetComponent< CTransform >( protoLook.aLookTarget, "transform" );

		// Assert( playerTransform );

		if ( !playerTransform )
		{
			// Find something else to try to look at
			protoLook.aTimeToDuel = 0.f;
			continue;
		}

		auto protoTransform = Ent_GetComponent< CTransform >( proto, "transform" );

		Assert( protoTransform );

		// glm::length( renderable->aModelMatrix ) == 0.f

		// TESTING: BROKEN
		if ( proto_look == 2.f )
		{
			glm::vec3 forward, right, up;
			//AngleToVectors( protoTransform.aAng, forward, right, up );
			// Util_GetDirectionVectors( playerTransform.aAng, &forward, &right, &up );
			glm::mat4 playerMatrix;
			Util_ToMatrix( playerMatrix, playerTransform->aPos, playerTransform->aAng );
			Util_GetMatrixDirection( playerMatrix, &forward, &right, &up );

			// Graphics_DrawLine( protoTransform.aPos, protoTransform.aPos + ( forward * r_proto_line_dist2.GetFloat() ), { 1.f, 0.f, 0.f } );
			// Graphics_DrawLine( protoTransform.aPos, protoTransform.aPos + ( right * r_proto_line_dist2.GetFloat() ), { 0.f, 1.f, 0.f } );
			// Graphics_DrawLine( protoTransform.aPos, protoTransform.aPos + ( up * r_proto_line_dist2.GetFloat() ), { 0.f, 0.f, 1.f } );

			glm::vec3 protoView    = protoTransform->aPos;
			//protoView.z += cl_view_height;

			glm::vec3 direction    = ( protoView - playerTransform->aPos.Get() );
			glm::vec3 rotationAxis = Util_VectorToAngles( direction, up );

			glm::mat4 protoViewMat = glm::lookAt( protoView, playerTransform->aPos.Get(), up );

			glm::vec3 vForward, vRight, vUp;
			Util_GetViewMatrixZDirection( protoViewMat, vForward, vRight, vUp );
		}
		else if ( proto_look.GetBool() && !Game_IsPaused() )
		{
			glm::vec3 up;
			Util_GetDirectionVectors( playerTransform->aAng, nullptr, nullptr, &up );

			glm::vec3        protoView           = protoTransform->aPos;
			glm::vec3        direction           = protoView - playerTransform->aPos.Get();
			glm::vec3        rotationAxis        = Util_VectorToAngles( direction, up );

			if ( targetChanged )
			{
				protoLook.aStartAng    = protoTransform->aAng;
				protoLook.aTurnEndTime = RandomFloat( proto_swap_turn_time_min, proto_swap_turn_time_max );
				protoLook.aTurnCurTime = 0.f;

				// Make sure we don't suddenly want to look at something else while midway through turning to look at something
				// Though if you want this behavior, you would need to have them slow down looking, and slowly start to look at that
				// Like a turn velocity or something lol
				protoLook.aTurnEndTime = std::min( protoLook.aTurnEndTime, protoLook.aTimeToDuel );
			}

			protoLook.aGoalAng          = rotationAxis;
			protoLook.aGoalAng[ PITCH ] = 0.f;
			protoLook.aGoalAng[ YAW ] -= 90.f;
			protoLook.aGoalAng[ ROLL ] = ( -rotationAxis[ PITCH ] ) + 90.f;

			protoLook.aTurnCurTime += frameTime;

			if ( protoLook.aTurnEndTime >= protoLook.aTurnCurTime )
			{
				float time           = protoLook.aTurnCurTime / protoLook.aTurnEndTime;
				float timeCurve      = Math_EaseInOutCubic( time );
				protoTransform->aAng = glm::mix( protoLook.aStartAng, protoLook.aGoalAng, timeCurve );
			}
			else
			{
				protoTransform->aAng = protoLook.aGoalAng;
			}
		}

		if ( protoTransform->aScale.Get().x != protoScale )
		{
			protoTransform->aScale = glm::vec3( protoScale );
		}

		if ( proto_follow.GetBool() && !Game_IsPaused() )
		{
			glm::vec3 modelUp, modelRight, modelForward;

			glm::mat4 protoMatrix;
			Util_ToMatrix( protoMatrix, protoTransform->aPos, protoTransform->aAng, protoTransform->aScale );

			Util_GetMatrixDirection( protoMatrix, &modelForward, &modelRight, &modelUp );
		
			ProtoTurn_t& protoTurn = GetProtogenSys()->aTurnMap[ proto ];
		
			if ( protoTurn.aNextTime <= gCurTime )
			{
				protoTurn.aNextTime = gCurTime + ( rand() / ( RAND_MAX / 8.f ) );
				protoTurn.aLastRand = protoTurn.aRand;
				// protoTurn.aRand     = ( rand() / ( RAND_MAX / 40.0f ) ) - 20.f;
				protoTurn.aRand     = ( rand() / ( RAND_MAX / 360.0f ) ) - 180.f;
			}
		
			float randTurn       = std::lerp( protoTurn.aLastRand, protoTurn.aRand, protoTurn.aNextTime - gCurTime );
		
			protoTransform->aPos = protoTransform->aPos + ( modelUp * proto_follow_speed.GetFloat() * gFrameTime );
		
			// protoTransform.aAng    = protoTransform.aAng + ( modelForward * randTurn * proto_follow_speed.GetFloat() * gFrameTime );
			protoTransform->aAng = protoTransform->aAng + ( modelRight * randTurn * proto_follow_speed.GetFloat() * gFrameTime );
		}
	}
#endif
}


CONVAR( snd_test_vol, 0.25 );

#if AUDIO_OPENAL
// idk what these values should really be tbh
// will require a lot of fine tuning tbh
CONVAR_CMD( snd_doppler_scale, 0.2 )
{
	audio->SetDopplerScale( snd_doppler_scale );
}

CONVAR_CMD( snd_sound_speed, 6000 )
{
	audio->SetSoundSpeed( snd_sound_speed );
}
#endif

CONCMD( snd_test )
{
	Handle stream = streams.emplace_back( audio->LoadSound( "sound/endymion_mono.ogg" ) );

	/*if ( audio->IsValid( stream ) )
	{
		audio->FreeSound( stream );
	}*/
	// test sound
	//else if ( stream = audio->LoadSound("sound/rain2.ogg") )
	if ( stream )
	//else if ( stream = audio->LoadSound("sound/endymion_mono.ogg") )
	//else if ( stream = audio->LoadSound("sound/endymion2.wav") )
	//else if ( stream = audio->LoadSound("sound/endymion_mono.wav") )
	//else if ( stream = audio->LoadSound("sound/robots_cropped.ogg") )
	{
		audio->SetVolume( stream, snd_test_vol );

#if AUDIO_OPENAL
		audio->AddEffect( stream, AudioEffect_Loop );  // on by default
#endif

		// play it where the player currently is
		// audio->AddEffect( stream, AudioEffect_World );
		// audio->SetEffectData( stream, Audio_World_Pos, transform.aPos );

		audio->PlaySound( stream );

		/*if ( g_streamModel == nullptr )
		{
			g_streamModel = graphics->LoadModel( "materials/models/cube.obj" );
			//aModels.push_back( g_streamModel );
		}

		g_streamModel->SetPos( transform.aPos );*/
	}
}

CONCMD( snd_test_clear )
{
	for ( Handle stream : streams )
	{
		if ( audio->IsValid( stream ) )
		{
			audio->FreeSound( stream );
		}
	}

	streams.clear();
}


void TEST_UpdateAudio()
{
	// auto& transform = GetEntitySystem()->GetComponent< Transform >( gLocalPlayer );

	for ( Handle stream : streams )
	{
		if ( audio->IsValid( stream ) )
		{
			audio->SetVolume( stream, snd_test_vol );
		}
	}
}



#include "main.h"
#include "game_shared.h"
#include "entity.h"
#include "world.h"
#include "util.h"
#include "player.h"  // TEMP - for CPlayerMoveData

#include "entity.h"
#include "entity_systems.h"
#include "mapmanager.h"
#include "igui.h"

#include "graphics/graphics.h"

#include "game_physics.h"  // just for IPhysicsShape* and IPhysicsObject*


// ====================================================================================================
// Base Components
// 
// TODO:
//  - add an option to define what components and component variables get saved to a map/scene
//  - also a random thought - maybe you could use a map/scene to create a preset entity prefab to create?
//      like a preset player entity to spawn in when a player loads
// ====================================================================================================


void Ent_RegisterVarHandlers()
{
}


CH_STRUCT_REGISTER_COMPONENT( CRigidBody, rigidBody, true, EEntComponentNetType_Both, true )
{
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aVel, vel, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aAccel, accel, true );
}


CH_STRUCT_REGISTER_COMPONENT( CDirection, direction, true, EEntComponentNetType_Both, true )
{
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aForward, forward, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aUp, up, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aRight, right, true );
}


// TODO: use a "protocol system", so an internally created model path would be this:
// "internal://model_0"
// and a file on the disk to load will be this:
// "file://path/to/asset.glb"
CH_STRUCT_REGISTER_COMPONENT( CRenderable, renderable, true, EEntComponentNetType_Both, true )
{
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_StdString, std::string, aPath, path, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aTestVis, testVis, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aCastShadow, castShadow, true );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aVisible, visible, true );
	
	CH_REGISTER_COMPONENT_SYS2( EntSys_Renderable, gEntSys_Renderable );
}


// Probably should be in graphics?
CH_STRUCT_REGISTER_COMPONENT( CLight, light, true, EEntComponentNetType_Both, CH_ENT_SAVE_TO_MAP )
{
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_S32, ELightType, aType, type, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Color4, glm::vec4, aColor, color, CH_ENT_SAVE_TO_MAP );

	// TODO: these 2 should not be here
	// it should be attached to it's own entity that can be parented
	// and that entity needs to contain the transform (or transform small) component
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aPos, pos, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Vec3, glm::vec3, aAng, ang, CH_ENT_SAVE_TO_MAP );

	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Float, float, aInnerFov, innerFov, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Float, float, aOuterFov, outerFov, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Float, float, aRadius, radius, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Float, float, aLength, length, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aShadow, shadow, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aEnabled, enabled, CH_ENT_SAVE_TO_MAP );
	CH_REGISTER_COMPONENT_VAR2( EEntNetField_Bool, bool, aUseTransform, useTransform, CH_ENT_SAVE_TO_MAP );
	
	CH_REGISTER_COMPONENT_SYS2( LightSystem, gLightEntSystems );
}


void Ent_RegisterBaseComponents()
{
	Ent_RegisterVarHandlers();

	// Setup Types, only used for registering variables without specifing the VarType
	gEntComponentRegistry.aVarTypes[ typeid( bool ).hash_code() ]        = EEntNetField_Bool;
	gEntComponentRegistry.aVarTypes[ typeid( float ).hash_code() ]       = EEntNetField_Float;
	gEntComponentRegistry.aVarTypes[ typeid( double ).hash_code() ]      = EEntNetField_Double;

	gEntComponentRegistry.aVarTypes[ typeid( s8 ).hash_code() ]          = EEntNetField_S8;
	gEntComponentRegistry.aVarTypes[ typeid( s16 ).hash_code() ]         = EEntNetField_S16;
	gEntComponentRegistry.aVarTypes[ typeid( s32 ).hash_code() ]         = EEntNetField_S32;
	gEntComponentRegistry.aVarTypes[ typeid( s64 ).hash_code() ]         = EEntNetField_S64;

	gEntComponentRegistry.aVarTypes[ typeid( u8 ).hash_code() ]          = EEntNetField_U8;
	gEntComponentRegistry.aVarTypes[ typeid( u16 ).hash_code() ]         = EEntNetField_U16;
	gEntComponentRegistry.aVarTypes[ typeid( u32 ).hash_code() ]         = EEntNetField_U32;
	gEntComponentRegistry.aVarTypes[ typeid( u64 ).hash_code() ]         = EEntNetField_U64;

	// probably overrides type have of u64, hmmm
	// gEntComponentRegistry.aVarTypes[ typeid( Entity ).hash_code() ]      = EEntNetField_Entity;
	gEntComponentRegistry.aVarTypes[ typeid( std::string ).hash_code() ] = EEntNetField_StdString;

	gEntComponentRegistry.aVarTypes[ typeid( glm::vec2 ).hash_code() ]   = EEntNetField_Vec2;
	gEntComponentRegistry.aVarTypes[ typeid( glm::vec3 ).hash_code() ]   = EEntNetField_Vec3;
	gEntComponentRegistry.aVarTypes[ typeid( glm::vec4 ).hash_code() ]   = EEntNetField_Vec4;

	// Now Register Base Components
	EntComp_RegisterComponent< CTransform >(
	  "transform", true, EEntComponentNetType_Both,
	  [ & ]()
	  {
		  auto transform           = new CTransform;
		  transform->aScale.Edit() = { 1.f, 1.f, 1.f };
		  return transform;
	  },
	  [ & ]( void* spData )
	  { delete (CTransform*)spData; }, true );

	EntComp_RegisterComponentVar< CTransform, glm::vec3 >( "pos", offsetof( CTransform, aPos ), typeid( CTransform::aPos ).hash_code(), true );
	EntComp_RegisterComponentVar< CTransform, glm::vec3 >( "ang", offsetof( CTransform, aAng ), typeid( CTransform::aAng ).hash_code(), true );
	EntComp_RegisterComponentVar< CTransform, glm::vec3 >( "scale", offsetof( CTransform, aScale ), typeid( CTransform::aScale ).hash_code(), true );
	CH_REGISTER_COMPONENT_SYS( CTransform, EntSys_Transform, gEntSys_Transform );

	// CH_REGISTER_COMPONENT_RW( CRigidBody, rigidBody, true );
	// CH_REGISTER_COMPONENT_VAR( CRigidBody, glm::vec3, aVel, vel );
	// CH_REGISTER_COMPONENT_VAR( CRigidBody, glm::vec3, aAccel, accel );

	// CH_REGISTER_COMPONENT_RW( CDirection, direction, true );
	// CH_REGISTER_COMPONENT_VAR( CDirection, glm::vec3, aForward, forward );
	// CH_REGISTER_COMPONENT_VAR( CDirection, glm::vec3, aUp, up );
	// // CH_REGISTER_COMPONENT_VAR( CDirection, glm::vec3, aRight, right );
	// CH_REGISTER_COMP_VAR_VEC3( CDirection, aRight, right );

	// CH_REGISTER_COMPONENT_RW( CGravity, gravity, true );
	// CH_REGISTER_COMP_VAR_VEC3( CGravity, aForce, force );

	// might be a bit weird
	// HACK HACK: DONT OVERRIDE CLIENT VALUE, IT WILL NEVER BE UPDATED
	CH_REGISTER_COMPONENT_RW( CCamera, camera, false, true );
	CH_REGISTER_COMPONENT_VAR( CCamera, float, aFov, fov, true );
	
	CH_REGISTER_COMPONENT( CMap, map, true, EEntComponentNetType_Both, false );
}


// Helper Functions
Handle Ent_GetRenderableHandle( Entity sEntity )
{
	auto renderComp = Ent_GetComponent< CRenderable >( sEntity, "renderable" );

	if ( !renderComp )
	{
		Log_Error( "Failed to get renderable component\n" );
		return InvalidHandle;
	}

	return renderComp->aRenderable;
}


Renderable_t* Ent_GetRenderable( Entity sEntity )
{
	auto renderComp = Ent_GetComponent< CRenderable >( sEntity, "renderable" );

	if ( !renderComp )
	{
		Log_Error( "Failed to get renderable component\n" );
		return nullptr;
	}

	return Graphics_GetRenderableData( renderComp->aRenderable );
}


// Requires the entity to have renderable component with a model path set
Renderable_t* Ent_CreateRenderable( Entity sEntity )
{
	auto renderComp = Ent_GetComponent< CRenderable >( sEntity, "renderable" );

	if ( !renderComp )
	{
		Log_Error( "Failed to get renderable component\n" );
		return nullptr;
	}

	if ( renderComp->aRenderable == InvalidHandle )
	{
		if ( renderComp->aModel == InvalidHandle )
		{
			renderComp->aModel = Graphics_LoadModel( renderComp->aPath );
			if ( renderComp->aModel == InvalidHandle )
			{
				Log_Error( "Failed to load model for renderable\n" );
				return nullptr;
			}
		}

		renderComp->aRenderable = Graphics_CreateRenderable( renderComp->aModel );
		if ( renderComp->aRenderable == InvalidHandle )
		{
			Log_Error( "Failed to create renderable\n" );
			return nullptr;
		}
	}

	return Graphics_GetRenderableData( renderComp->aRenderable );
}


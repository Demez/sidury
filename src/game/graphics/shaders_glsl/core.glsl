#ifndef CH_CORE_GLSL
#define CH_CORE_GLSL

#define CH_DESC_SET_GLOBAL       0
#define CH_DESC_SET_PER_SHADER   1

// KEEP IN SYNC WITH GAME CODE
// TODO: make some file you can share between shader code and c++
#define CH_LIGHT_TYPES           3  // TODO: 4 when you add capsule lights

#define CH_R_MAX_TEXTURES        4096
#define CH_R_MAX_VIEWPORTS       32
#define CH_R_MAX_RENDERABLES     4096
#define CH_R_MAX_MATERIALS       64  // max materials per renderable
#define CH_R_MAX_SURFACE_DRAWS   CH_R_MAX_VIEWPORTS * CH_R_MAX_RENDERABLES * CH_R_MAX_MATERIALS
#define CH_R_MAX_LIGHT_TYPE      256
#define CH_R_MAX_LIGHTS          CH_R_MAX_LIGHT_TYPE * CH_LIGHT_TYPES

#define CH_R_LIGHT_LIST_SIZE     CH_R_MAX_LIGHTS

#define CH_BINDING_TEXTURES      0
#define CH_BINDING_CORE          1
#define CH_BINDING_SURFACE_DRAWS 2

// ===================================================================================
// Base Structs


struct Viewport_t
{
	mat4  aProjView;
	mat4  aProjection;
	mat4  aView;
	vec3  aViewPos;
	float aNearZ;
	float aFarZ;
};


// shared renderable data
struct Renderable_t
{
	mat4 aModel;

    // uint aMaterialCount;
    // uint aMaterials[ CH_R_MAX_MATERIALS ];

    uint aVertexBuffer;
    uint aIndexBuffer;

	uint aLightCount;
	// uint aLightList[ CH_R_LIGHT_LIST_SIZE ];
};


// surface index and renderable index
// unique to each viewport
// basically, "Draw the current renderable surface with this material"
struct SurfaceDraw_t
{
	uint aRenderable;  // index into gCore.aRenderables
	uint aMaterial;    // index into the shader material array
};


// Lights - Need to improve this
struct LightWorld_t
{
    vec4 aColor;
    vec3 aDir;
    mat4 aProjView;  // viewport for light/shadow
	int  aShadow;    // shadow texture index
};


struct LightPoint_t
{
	vec4  aColor;
	vec3  aPos;
	float aRadius;  // TODO: look into using constant, linear, and quadratic lighting, more customizable than this
};


struct LightCone_t
{
	vec4 aColor;
	vec3 aPos;
	vec3 aDir;
	vec2 aFov;       // x is inner FOV, y is outer FOV
	mat4 aProjView;  // viewport for light/shadow
	int  aShadow;    // shadow texture index
};


struct LightCapsule_t
{
	vec4  aColor;
	vec3  aDir;
	float aLength;
	float aThickness;
};


// ===================================================================================
#ifdef CH_FRAG_SHADER


layout(set = 0, binding = CH_BINDING_TEXTURES) uniform sampler2D[]       texSamplers;
layout(set = 0, binding = CH_BINDING_TEXTURES) uniform sampler2DArray[]  texSamplerArray;
layout(set = 0, binding = CH_BINDING_TEXTURES) uniform sampler2DShadow[] texShadowMaps;


#endif // CH_FRAG_SHADER
// ===================================================================================


// TODO: probably split this a little bit, this could be very hefty to update,
// unless you make use of only updating a section of a storage buffer
layout(set = 0, binding = CH_BINDING_CORE) buffer readonly Buffer_Core
{
	uint           aNumTextures;
	uint           aNumViewports;
	uint           aNumVertexBuffers;
	uint           aNumIndexBuffers;

	uint           aNumLights[ CH_LIGHT_TYPES ];

	Renderable_t   aRenderables[ CH_R_MAX_RENDERABLES ];
	Viewport_t     aViewports[ CH_R_MAX_VIEWPORTS ];

	LightWorld_t   aLightWorld[ CH_R_MAX_LIGHT_TYPE ];
	LightPoint_t   aLightPoint[ CH_R_MAX_LIGHT_TYPE ];
	LightCone_t    aLightCone[ CH_R_MAX_LIGHT_TYPE ];
	LightCapsule_t aLightCapsule[ CH_R_MAX_LIGHT_TYPE ];
} gCore;


layout(set = 0, binding = CH_BINDING_SURFACE_DRAWS) buffer readonly Buffer_SurfaceDraws
{
	SurfaceDraw_t gSurfaceDraws[ CH_R_MAX_SURFACE_DRAWS ];
};


// ===================================================================================

// https://stackoverflow.com/questions/56428880/how-to-extract-camera-parameters-from-projection-matrix
// fov  = 2 * atan(1/camera.projectionMatrix.elements[5]) * 180 / PI;
// near = camera.projectionMatrix.elements[14] / (camera.projectionMatrix.elements[10] - 1.0);
// far  = camera.projectionMatrix.elements[14] / (camera.projectionMatrix.elements[10] + 1.0);


#endif // CH_CORE_GLSL


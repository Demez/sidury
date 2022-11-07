#include "util.h"
#include "render/irender.h"
#include "graphics.h"


extern IRender*                             render;

static Handle                               gFallbackAO            = InvalidHandle;
static Handle                               gFallbackEmissive      = InvalidHandle;

constexpr const char*                       gpFallbackAOPath       = "materials/base/white.ktx";
constexpr const char*                       gpFallbackEmissivePath = "materials/base/black.ktx";

// descriptor set layouts
extern UniformBufferArray_t                 gUniformMaterialBasic3D;

// Material Handle, Buffer
static std::unordered_map< Handle, Handle > gMaterialBuffers;
static std::vector< Handle >                gMaterialBufferIndex;

constexpr EShaderFlags                      gShaderFlags =
  EShaderFlags_Sampler |
  EShaderFlags_ViewInfo |
  EShaderFlags_PushConstant |
  EShaderFlags_MaterialUniform |
  EShaderFlags_Lights;


struct Basic3D_Push
{
	alignas( 16 ) glm::mat4 aModelMatrix{};  // model matrix
	alignas( 16 ) int aMaterial = 0;         // material index
	int aProjView = 0;         // projection * view index

	// debugging
	int aDebugDraw;
};


struct Basic3D_Material
{
	int   diffuse       = 0;
	int   ao            = 0;
	int   emissive      = 0;

	float aoPower       = 1.f;
	float emissivePower = 1.f;
};


static std::unordered_map< ModelSurfaceDraw_t*, Basic3D_Push > gPushData;
static std::unordered_map< Handle, Basic3D_Material >          gMaterialData;


CONVAR( r_basic3d_dbg_mode, 0 );


EShaderFlags Shader_Basic3D_Flags()
{
	return EShaderFlags_Sampler | EShaderFlags_ViewInfo | EShaderFlags_PushConstant | EShaderFlags_MaterialUniform | EShaderFlags_Lights;
}


static bool Shader_Basic3D_Init()
{
	TextureCreateData_t createData{};
	createData.aFilter = EImageFilter_Nearest;
	createData.aUsage  = EImageUsage_Sampled;

	// create fallback textures
	render->LoadTexture( gFallbackAO, gpFallbackAOPath, createData );
	render->LoadTexture( gFallbackEmissive, gpFallbackEmissivePath, createData );

	return true;
}


static void Shader_Basic3D_Destroy()
{
	render->FreeTexture( gFallbackAO );
	render->FreeTexture( gFallbackEmissive );

	Graphics_SetAllMaterialsDirty();
}


static void Shader_Basic3D_GetPipelineLayoutCreate( PipelineLayoutCreate_t& srPipeline )
{
	Graphics_AddPipelineLayouts( srPipeline, Shader_Basic3D_Flags() );
	srPipeline.aLayouts.push_back( gUniformMaterialBasic3D.aLayout );
	srPipeline.aPushConstants.emplace_back( ShaderStage_Vertex | ShaderStage_Fragment, 0, sizeof( Basic3D_Push ) );
}


static void Shader_Basic3D_GetGraphicsPipelineCreate( GraphicsPipelineCreate_t& srGraphics )
{
	srGraphics.aShaderModules.emplace_back( ShaderStage_Vertex, "shaders/basic3d.vert.spv", "main" );
	srGraphics.aShaderModules.emplace_back( ShaderStage_Fragment, "shaders/basic3d.frag.spv", "main" );

	srGraphics.aColorBlendAttachments.emplace_back( false ); 

	srGraphics.aPrimTopology   = EPrimTopology_Tri;
	srGraphics.aDynamicState   = EDynamicState_Viewport | EDynamicState_Scissor;
	srGraphics.aCullMode       = ECullMode_Back;
}


static void Shader_Basic3D_ResetPushData()
{
	gPushData.clear();
}


static void Shader_Basic3D_SetupPushData( ModelSurfaceDraw_t& srDrawInfo )
{
	Basic3D_Push& push = gPushData[ &srDrawInfo ];
	push.aModelMatrix  = srDrawInfo.apDraw->aModelMatrix;

	Handle mat         = Model_GetMaterial( srDrawInfo.apDraw->aModel, srDrawInfo.aSurface );
	// push.aMaterial     = GET_HANDLE_INDEX( mat );
	// push.aMaterial     = gMaterialBufferIndex[ mat ];
	push.aMaterial     = vec_index( gMaterialBufferIndex, mat );

	push.aProjView     = 0;
	push.aDebugDraw    = r_basic3d_dbg_mode;
}


static void Shader_Basic3D_PushConstants( Handle cmd, Handle sLayout, ModelSurfaceDraw_t& srDrawInfo )
{
	Basic3D_Push& push = gPushData.at( &srDrawInfo );
	render->CmdPushConstants( cmd, sLayout, ShaderStage_Vertex | ShaderStage_Fragment, 0, sizeof( Basic3D_Push ), &push );
}


static IShaderPush gShaderPush_Basic3D = {
	.apReset = Shader_Basic3D_ResetPushData,
	.apSetup = Shader_Basic3D_SetupPushData,
	.apPush  = Shader_Basic3D_PushConstants,
};


ShaderCreate_t gShaderCreate_Basic3D = {
	.apName           = "basic_3d",
	.aStages          = ShaderStage_Vertex | ShaderStage_Fragment,
	.aBindPoint       = EPipelineBindPoint_Graphics,
	.aFlags           = EShaderFlags_Sampler | EShaderFlags_ViewInfo | EShaderFlags_PushConstant | EShaderFlags_MaterialUniform | EShaderFlags_Lights,
	.aVertexFormat    = VertexFormat_Position | VertexFormat_Normal | VertexFormat_TexCoord,
	.apInit           = Shader_Basic3D_Init,
	.apDestroy        = Shader_Basic3D_Destroy,
	.apLayoutCreate   = Shader_Basic3D_GetPipelineLayoutCreate,
	.apGraphicsCreate = Shader_Basic3D_GetGraphicsPipelineCreate,
	.apShaderPush     = &gShaderPush_Basic3D,
};


bool Shader_Basic3D_CreateMaterialBuffer( Handle sMat )
{
	// IDEA: since materials shouldn't be updated very often,
	// maybe have the buffer be on the gpu (EBufferMemory_Device)?

	Handle buffer = render->CreateBuffer( Mat_GetName( sMat ), sizeof( Basic3D_Material ), EBufferFlags_Uniform, EBufferMemory_Host );

	if ( buffer == InvalidHandle )
	{
		Log_Error( gLC_ClientGraphics, "Failed to Create Material Uniform Buffer\n" );
		return false;
	}

	gMaterialBuffers[ sMat ] = buffer;

	// update the material descriptor sets
	UpdateVariableDescSet_t update{};

	// what
	update.aDescSets.push_back( gUniformMaterialBasic3D.aSets[ 0 ] );
	update.aDescSets.push_back( gUniformMaterialBasic3D.aSets[ 1 ] );

	update.aType = EDescriptorType_UniformBuffer;

	// blech
	gMaterialBufferIndex.clear();
	for ( const auto& [ mat, buffer ] : gMaterialBuffers )
	{
		update.aBuffers.push_back( buffer );
		gMaterialBufferIndex.push_back( mat );
	}

	// update.aBuffers = gMaterialBuffers[ sMat ];
	render->UpdateVariableDescSet( update );

	return true;
}


// TODO: this doesn't handle shaders being changed on materials, or materials being freed
void Shader_Basic3D_UpdateMaterialData( Handle sMat )
{
	Basic3D_Material* mat = nullptr;

	auto it = gMaterialData.find( sMat );

	if ( it != gMaterialData.end() )
	{
		mat = &it->second;
	}
	else
	{
		// New Material Using this shader
		if ( !Shader_Basic3D_CreateMaterialBuffer( sMat ) )
			return;

		// create new material data
		mat = &gMaterialData[ sMat ];
	}

	mat->diffuse       = Mat_GetTextureIndex( sMat, "diffuse" );
	mat->ao            = Mat_GetTextureIndex( sMat, "ao", gFallbackAO );
	mat->emissive      = Mat_GetTextureIndex( sMat, "emissive", gFallbackEmissive );

	mat->aoPower       = Mat_GetFloat( sMat, "aoPower", 0.f );
	mat->emissivePower = Mat_GetFloat( sMat, "emissivePower", 0.f );

	// write new material data to the buffer
	Handle buffer = gMaterialBuffers[ sMat ];
	render->MemWriteBuffer( buffer, sizeof( Basic3D_Material ), mat );
}


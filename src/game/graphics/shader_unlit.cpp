#include "util.h"
#include "render/irender.h"
#include "graphics.h"


struct ShaderUnlit_Push
{
	alignas( 16 ) glm::mat4 aModelMatrix{};  // model matrix
	alignas( 16 ) int aProjView = 0;         // projection * view index
	int aDiffuse                = 0;         // aldedo
};


static std::unordered_map< SurfaceDraw_t*, ShaderUnlit_Push >  gPushData;


constexpr EShaderFlags Shader_ShaderUnlit_Flags()
{
	return EShaderFlags_Sampler | EShaderFlags_ViewInfo | EShaderFlags_PushConstant;
}


static void Shader_ShaderUnlit_GetPipelineLayoutCreate( PipelineLayoutCreate_t& srPipeline )
{
	Graphics_AddPipelineLayouts( srPipeline, Shader_ShaderUnlit_Flags() );
	srPipeline.aPushConstants.emplace_back( ShaderStage_Vertex | ShaderStage_Fragment, 0, sizeof( ShaderUnlit_Push ) );
}


static void Shader_ShaderUnlit_GetGraphicsPipelineCreate( GraphicsPipelineCreate_t& srGraphics )
{
	srGraphics.aShaderModules.emplace_back( ShaderStage_Vertex, "shaders/unlit.vert.spv", "main" );
	srGraphics.aShaderModules.emplace_back( ShaderStage_Fragment, "shaders/unlit.frag.spv", "main" );

	srGraphics.aColorBlendAttachments.emplace_back( false ); 

	srGraphics.aPrimTopology   = EPrimTopology_Tri;
	srGraphics.aDynamicState   = EDynamicState_Viewport | EDynamicState_Scissor;
	srGraphics.aCullMode       = ECullMode_Back;
}


static void Shader_ShaderUnlit_ResetPushData()
{
	gPushData.clear();
}


static void Shader_ShaderUnlit_SetupPushData( Renderable_t* spModelDraw, SurfaceDraw_t& srDrawInfo )
{
	PROF_SCOPE();

	ShaderUnlit_Push& push = gPushData[ &srDrawInfo ];
	push.aModelMatrix      = spModelDraw->aModelMatrix;
	push.aProjView         = 0;

	Handle mat             = Model_GetMaterial( spModelDraw->aModel, srDrawInfo.aSurface );

	if ( mat == CH_INVALID_HANDLE )
		return;

	push.aDiffuse = Mat_GetTextureIndex( mat, "diffuse" );
}


static void Shader_ShaderUnlit_PushConstants( Handle cmd, Handle sLayout, SurfaceDraw_t& srDrawInfo )
{
	PROF_SCOPE();

	ShaderUnlit_Push& push = gPushData.at( &srDrawInfo );
	render->CmdPushConstants( cmd, sLayout, ShaderStage_Vertex | ShaderStage_Fragment, 0, sizeof( ShaderUnlit_Push ), &push );
}


static IShaderPush gShaderPush_Unlit = {
	.apReset = Shader_ShaderUnlit_ResetPushData,
	.apSetup = Shader_ShaderUnlit_SetupPushData,
	.apPush  = Shader_ShaderUnlit_PushConstants,
};


ShaderCreate_t gShaderCreate_Unlit = {
	.apName           = "unlit",
	.aStages          = ShaderStage_Vertex | ShaderStage_Fragment,
	.aBindPoint       = EPipelineBindPoint_Graphics,
	.aFlags           = Shader_ShaderUnlit_Flags(),
	.aDynamicState    = EDynamicState_Viewport | EDynamicState_Scissor,
	.aVertexFormat    = VertexFormat_Position | VertexFormat_TexCoord,
	.apInit           = nullptr,
	.apDestroy        = nullptr,
	.apLayoutCreate   = Shader_ShaderUnlit_GetPipelineLayoutCreate,
	.apGraphicsCreate = Shader_ShaderUnlit_GetGraphicsPipelineCreate,
	.apShaderPush     = &gShaderPush_Unlit,
};


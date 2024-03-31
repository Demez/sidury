#include "main.h"

#include "core/app_info.h"
#include "core/profiler.h"

#include "iinput.h"
#include "render/irender.h"
#include "igraphics.h"
#include "iaudio.h"
#include "igui.h"
#include "physics/iphysics.h"

#include "imgui/imgui.h"

#include <chrono>
#include <vector>
#include <functional>

#if CH_USE_MIMALLOC
  #include "mimalloc-new-delete.h"
#endif

static bool        gWaitForDebugger = Args_Register( "Upon Program Startup, Wait for the Debugger to attach", "-debugger" );
static const char* gArgGamePath     = Args_Register( nullptr, "Path to the game to create assets for", "-game" );
static bool        gRunning         = true;

CONVAR( host_max_frametime, 0.1 );
CONVAR( host_timescale, 1 );
CONVAR( host_fps_max, 300 );


CONCMD( exit )
{
	gRunning = false;
}

CONCMD( quit )
{
	gRunning = false;
}

#if CH_USE_MIMALLOC
CONCMD( mimalloc_print )
{
	// TODO: have this output to the logging system
	mi_collect( true );
	mi_stats_merge();
	mi_stats_print( nullptr );
}
#endif


extern IGuiSystem*       gui;
extern IRender*          render;
extern IInputSystem*     input;
extern IAudioSystem*     audio;
extern Ch_IPhysics*      ch_physics;
extern IGraphics*        graphics;
extern IRenderSystemOld* renderOld;

extern ITool*            toolMapEditor;
extern ITool*            toolMatEditor;


static AppModule_t gAppModules[] = 
{
	{ (ISystem**)&input,      "ch_input",           IINPUTSYSTEM_NAME, IINPUTSYSTEM_HASH },
	{ (ISystem**)&render,     "ch_graphics_api_vk", IRENDER_NAME, IRENDER_VER },
	{ (ISystem**)&audio,      "ch_aduio",           IADUIO_NAME, IADUIO_VER },
	{ (ISystem**)&ch_physics, "ch_physics",         IPHYSICS_NAME, IPHYSICS_HASH },
    { (ISystem**)&graphics,   "ch_render",          IGRAPHICS_NAME, IGRAPHICS_VER },
    { (ISystem**)&renderOld,  "ch_render",          IRENDERSYSTEMOLD_NAME, IRENDERSYSTEMOLD_VER },
	{ (ISystem**)&gui,        "ch_gui",             IGUI_NAME, IGUI_HASH },

	// Tools
    // { (ISystem**)&toolMapEditor, "modules/ch_map_editor", CH_TOOL_MAP_EDITOR_NAME, CH_TOOL_MAP_EDITOR_VER },
    { (ISystem**)&toolMatEditor, "modules/ch_material_editor", CH_TOOL_MAT_EDITOR_NAME, CH_TOOL_MAT_EDITOR_VER },
};


static void ShowInvalidGameOptionWindow( const char* spMessage )
{
	Log_ErrorF( 1, "%s\n", spMessage );
}


extern "C"
{
	void DLL_EXPORT game_init()
	{
		if ( gWaitForDebugger )
			sys_wait_for_debugger();

		srand( (unsigned int)time( 0 ) );  // setup rand(  )

		if ( gArgGamePath == nullptr || gArgGamePath == "" )
		{
			ShowInvalidGameOptionWindow( "No Game Specified" );
			return;
		}

		// Load the game's app info
		if ( !Core_AddAppInfo( FileSys_GetExePath() + PATH_SEP_STR + gArgGamePath ) )
		{
			ShowInvalidGameOptionWindow( "Failed to Load App Info" );
			return;
		}

		IMGUI_CHECKVERSION();

#if CH_USE_MIMALLOC
		Log_DevF( 1, "Using mimalloc version %d\n", mi_version() );
#endif

		// Needs to be done before Renderer is loaded
		ImGui::CreateContext();

		// if ( gArgUseGL )
		// {
		// 	gAppModules[ 1 ].apModuleName = "ch_render_gl";
		// }

		// Load Modules and Initialize them in this order
		if ( !Mod_AddSystems( gAppModules, ARR_SIZE( gAppModules ) ) )
		{
			Log_Error( "Failed to Load Systems\n" );
			return;
		}

		if ( !App_CreateMainWindow() )
		{
			Log_Error( "Failed to Create Main Window\n" );
			return;
		}

		if ( !Mod_InitSystems() )
		{
			Log_Error( "Failed to Init Systems\n" );
			return;
		}

		if ( !App_Init() )
		{
			Log_Error( "Failed to Start Editor!\n" );
			return;
		}

		Con_QueueCommandSilent( "exec autoexec", false );

		// ftl::TaskSchedulerInitOptions schedOptions;
		// schedOptions.Behavior = ftl::EmptyQueueBehavior::Sleep;
		// 
		// gTaskScheduler.Init( schedOptions );
		
		auto startTime = std::chrono::high_resolution_clock::now();

		// -------------------------------------------------------------------
		// Main Loop

		while ( gRunning )
		{
			PROF_SCOPE_NAMED( "Main Loop" );

			// TODO: REPLACE THIS, it's actually kinda expensive
			auto currentTime = std::chrono::high_resolution_clock::now();
			float time = std::chrono::duration< float, std::chrono::seconds::period >( currentTime - startTime ).count();
	
			// don't let the time go too crazy, usually happens when in a breakpoint
			time = glm::min( time, host_max_frametime.GetFloat() );

			if ( host_fps_max.GetFloat() > 0.f )
			{
				float maxFps = glm::clamp( host_fps_max.GetFloat(), 10.f, 5000.f );

				// check if we still have more than 2ms till next frame and if so, wait for "1ms"
				float minFrameTime = 1.0f / maxFps;
				if ( (minFrameTime - time) > (2.0f/1000.f))
					sys_sleep( 1 );

				// framerate is above max
				if ( time < minFrameTime )
					continue;
			}

			// ftl::TaskCounter taskCounter( &gTaskScheduler );

			input->Update( time );

			// may change from input update running the quit command
			if ( !gRunning )
				break;

			UpdateLoop( time );
			
			// Wait and help to execute unfinished tasks
			// gTaskScheduler.WaitForCounter( &taskCounter );

			startTime = currentTime;

#ifdef TRACY_ENABLE
			FrameMark;
#endif
		}
	}
}


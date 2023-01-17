#include "core/platform.h"
#include <stdio.h>

#if CH_USE_MIMALLOC
  #include "mimalloc-new-delete.h"
#endif

#include <SDL2/SDL_loadso.h>

Module core = 0;
Module imgui = 0;
Module client = 0;


void unload_objects()
{
	if ( core )     SDL_UnloadObject( core );
	if ( imgui )    SDL_UnloadObject( imgui );
	if ( client )   SDL_UnloadObject( client );
}


int load_object( Module* mod, const char* path )
{
	if ( *mod = SDL_LoadObject( path ) )
		return 0;

	fprintf( stderr, "Failed to load %s: %s\n", path, SDL_GetError() );
	//sys_print_last_error( "Failed to load %s", path );

	unload_objects(  );

	return -1;
}


#define GAME_PATH "sidury"

#if CH_USE_MIMALLOC
// ensure mimalloc is loaded
struct ForceMiMalloc_t
{
	ForceMiMalloc_t() { mi_version(); }
};

static ForceMiMalloc_t forceMiMalloc;
#endif


int main( int argc, char *argv[] )
{
	void ( *game_init )() = 0;
	void ( *core_init )( int argc, char *argv[], const char* gamePath ) = 0;
	void ( *core_exit )() = 0;

	if ( load_object( &core, "bin/core" EXT_DLL ) == -1 )
		return -1;
	if ( load_object( &imgui, "bin/imgui" EXT_DLL ) == -1 )
		return -1;
	if ( load_object( &client, GAME_PATH "/bin/client" EXT_DLL ) == -1 )
		return -1;

	*( void** )( &core_init ) = SDL_LoadFunction( core, "core_init" );
	if ( !core_init )
	{
		fprintf( stderr, "Error: %s\n", SDL_GetError() );
		unload_objects();
		return -1;
	}

	*( void** )( &core_exit ) = SDL_LoadFunction( core, "core_exit" );
	if ( !core_exit )
	{
		fprintf( stderr, "Error: %s\n", SDL_GetError() );
		unload_objects();
		return -1;
	}

	core_init( argc, argv, GAME_PATH );

	*( void** )( &game_init ) = SDL_LoadFunction( client, "game_init" );
	if ( !game_init )
	{
		fprintf( stderr, "Error: %s\n", SDL_GetError() );
		unload_objects();
		return -1;
	}

	game_init();
	core_exit();
	unload_objects();

	return 0;
}
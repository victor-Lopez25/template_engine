#define DLL_EXPORT static
#include "app.c"

#define VICLIB_PATH "src/viclib.h"
#define VL_BUILD_IMPLEMENTATION
#include "../vl_build.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    VL_SetCurrentDir(VL_temp_DirName(VL_temp_RunningExecutablePath()));

    void *appMemory = SDL_malloc(MemorySize());
    if(!appMemory) {
        fprintf(stderr, "Could not allocate memory for app");
        return 1;
    }

    SDL_memset4(appMemory, 0, MemorySize()/4);
    if(!InitAll(appMemory)) {
        SDL_free(appMemory);
        return 1;
    }

    bool quit = false;
    while(!quit) {
        quit = MainLoop(appMemory);
    }

    DeInitAll(appMemory);
    SDL_free(appMemory);

    return 0;
}

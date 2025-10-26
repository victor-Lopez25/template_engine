#define DLL_EXPORT static
#include "app.c"

#define NOB_IMPLEMENTATION
#include "../nob.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    nob_set_current_dir(nob_get_executable_dir_temp());

    void *appMemory = malloc(MemorySize());
    if(!appMemory) {
        fprintf(stderr, "Could not allocate memory for app");
        return 1;
    }

    SDL_memset4(appMemory, 0, MemorySize()/4);
    if(!InitAll(appMemory)) {
        free(appMemory);
        return 1;
    }

    bool quit = false;
    while(!quit) {
        quit = MainLoop(appMemory);
    }

    DeInitAll(appMemory);
    free(appMemory);

    return 0;
}

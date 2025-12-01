#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define VICLIB_PATH "src/viclib.h"
#define VL_BUILD_IMPLEMENTATION
#include "../vl_build.h"

#if defined(_WIN32)
# define DLL_EXT ".dll"
# ifndef LOCK_FILE_NAME
#  define LOCK_FILE_NAME "lock.tmp"
# endif
#elif __APPLE__
# define DLL_EXT ".dynlib"
#else
# define DLL_EXT ".so"
#endif

#define DLL_DIR "hotreload"

#define DLL_NAME "app" DLL_EXT

typedef bool (*BoolVoidStarProc)(void*);
typedef size_t (*MemorySizeProc)(void);
typedef void (*VoidStarProc)(void*);

typedef struct {
#if defined(_WIN32)
    HMODULE library;
#else
    void *library;
#endif

    BoolVoidStarProc initAll;
    BoolVoidStarProc initPartial;
    VoidStarProc deInitAll;
    VoidStarProc deInitPartial;
    MemorySizeProc memorySize;
    BoolVoidStarProc mainLoop;

    uint64_t modificationTime;
    int version;
} ProgramApi;

#if defined(_WIN32)
#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
bool LoadProgramApi(ProgramApi *api, int version)
{
    bool ok = false;
    WIN32_FILE_ATTRIBUTE_DATA ignore;

    if(!GetFileAttributesExA(LOCK_FILE_NAME, GetFileExInfoStandard, &ignore)) {
        if(GetLastWriteTime(DLL_NAME, &api->modificationTime)) {
            size_t mark = temp_save();
            char *dllname = temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, version);
            temp_rewind(mark);
            CopyFile(DLL_NAME, dllname, FALSE);

            api->library = LoadLibraryA(dllname);
            if(api->library) {
                api->initAll = (BoolVoidStarProc)(void*)GetProcAddress(api->library, "InitAll");
                api->initPartial = (BoolVoidStarProc)(void*)GetProcAddress(api->library, "InitPartial");
                api->deInitAll = (VoidStarProc)(void*)GetProcAddress(api->library, "DeInitAll");
                api->deInitPartial = (VoidStarProc)(void*)GetProcAddress(api->library, "DeInitPartial");
                
                api->memorySize = (MemorySizeProc)(void*)GetProcAddress(api->library, "MemorySize");
                api->mainLoop = (BoolVoidStarProc)(void*)GetProcAddress(api->library, "MainLoop");

                ok = api->initAll && api->initPartial && api->deInitAll && api->deInitPartial && api->memorySize && api->mainLoop;

                if(ok) api->version = version;
            } else {
                printf("%s\n", Win32_ErrorMessage(GetLastError()));
            }
        }
    }

    return ok;
}

void UnloadApi(ProgramApi *api)
{
    if(api->library) {
        if(!FreeLibrary(api->library)) {
            fprintf(stderr, "Could not unload " DLL_NAME ": %lu", GetLastError());
        }
    }
    size_t mark = temp_save();
    if(!DeleteFile(temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, api->version))) {
        fprintf(stderr, "Could not delete dll file");
    }
    temp_rewind(mark);
}
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

bool LoadProgramApi(ProgramApi *api, int version)
{
    bool ok = false;

    if(GetLastWriteTime(DLL_NAME, &api->modificationTime)) {
        size_t mark = nob_temp_save();
        char *dllname = nob_temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, version);
        nob_temp_rewind(mark);
        if(VL_CopyFile(DLL_NAME, dllname, FALSE)) {
            api->library = dlopen(dllname, RTLD_NOW);
            if(api->library) {
                api->initAll = (BoolVoidStarProc)dlsym(api->library, "InitAll");
                api->initPartial = (BoolVoidStarProc)dlsym(api->library, "InitPartial");
                api->deInitAll = (VoidStarProc)dlsym(api->library, "DeInitAll");
                api->deInitPartial = (VoidStarProc)dlsym(api->library, "DeInitPartial");
                
                api->memorySize = (MemorySizeProc)dlsym(api->library, "MemorySize");
                api->mainLoop = (BoolVoidStarProc)dlsym(api->library, "MainLoop");

                ok = api->initAll && api->initPartial && api->deInitAll && api->deInitPartial && api->memorySize && api->mainLoop;

                if(ok) api->version = version;
            }
        }
    }

    return ok;
}

void UnloadApi(ProgramApi *api)
{
    if(api->library) {
        if(dlclose(api->library)) {
            fprintf(stderr, "Could not unload " DLL_NAME ": %s", dlerror());
        }
    }
    size_t mark = temp_save();
    if(unlink(temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, api->version))) {
        fprintf(stderr, "Could not delete dll file");
    }
    temp_rewind(mark);
}
#endif

typedef struct {
    ProgramApi *items;
    size_t count;
    size_t capacity;
} ProgramApis;

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *exe_dir = VL_temp_DirName(VL_temp_RunningExecutablePath());
    VL_SetCurrentDir(exe_dir);

    int version = 0;
    ProgramApi api = {0};
    bool ok = LoadProgramApi(&api, version);
    if(!ok) {
        fprintf(stderr, "Could not load program api\n");
        return 1;
    }

    version++;
    void *appMemory = malloc(api.memorySize());
    if(!appMemory) {
        fprintf(stderr, "Could not allocate memory for app");
        return 1;
    }

    memset(appMemory, 0, api.memorySize());
    if(!api.initAll(appMemory)) {
        free(appMemory);
        UnloadApi(&api);
        return 1;
    }

    ProgramApis oldApis = {0};
    ProgramApi newApi = {0};
    bool quit = false;
    while(!quit) {
        uint64_t fileTime;

        if(GetLastWriteTime(DLL_NAME, &fileTime) && api.modificationTime != fileTime) {
            if(LoadProgramApi(&newApi, version)) {
                if(api.memorySize() == newApi.memorySize()) {
                    // normal hot reload
                    da_Append(&oldApis, api);
                    api.deInitPartial(appMemory);
                    memcpy(&api, &newApi, sizeof(ProgramApi));
                    api.initPartial(appMemory);
                } else {
                    // Full reset since we need a different amount of memory
                    api.deInitAll(appMemory);

                    for(size_t apiIdx = 0; apiIdx < oldApis.count; apiIdx++) {
                        UnloadApi(&oldApis.items[apiIdx]);
                    }
                    oldApis.count = 0;
                    memcpy(&api, &newApi, sizeof(ProgramApi));
                    free(appMemory);
                    appMemory = malloc(api.memorySize());
                    if(!appMemory) {
                        fprintf(stderr, "Could not alloc memory for program");
                        return 1;
                    }

                    memset(appMemory, 0, api.memorySize());
                    if(!api.initAll(appMemory)) {
                        goto endProgram;
                    }
                }

                version++;
            }
        }

        quit = api.mainLoop(appMemory);
    }

    api.deInitAll(appMemory);
    free(appMemory);

endProgram:
    for(size_t apiIdx = 0; apiIdx < oldApis.count; apiIdx++) {
        UnloadApi(&oldApis.items[apiIdx]);
    }
    da_Free(oldApis);

    return 0;
}

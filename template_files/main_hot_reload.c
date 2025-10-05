#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define NOB_IMPLEMENTATION
#include "../nob.h"

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
bool GetLastWriteTime(const char *fileName, uint64_t *writeTime)
{
    bool ok = false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(GetFileAttributesEx(fileName, GetFileExInfoStandard, &data) &&
        (uint64_t)data.nFileSizeHigh + (uint64_t)data.nFileSizeLow > 0)
    {
        *writeTime = *(uint64_t*)&data.ftLastWriteTime;
        ok = true;
    }
    return ok;
}

bool LoadProgramApi(ProgramApi *api, int version)
{
    bool ok = false;
    WIN32_FILE_ATTRIBUTE_DATA ignore;

    if(!GetFileAttributesExA(LOCK_FILE_NAME, GetFileExInfoStandard, &ignore)) {
        if(GetLastWriteTime(DLL_NAME, &api->modificationTime)) {
            size_t mark = nob_temp_save();
            char *dllname = nob_temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, version);
            nob_temp_rewind(mark);
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
                printf("%s\n", nob_win32_error_message(GetLastError()));
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
    size_t mark = nob_temp_save();
    if(!DeleteFile(nob_temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, api->version))) {
        fprintf(stderr, "Could not delete dll file");
    }
    nob_temp_rewind(mark);
}
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

bool GetLastWriteTime(const char *fileName, uint64_t *writeTime)
{
    struct stat attr;
    bool ok = false;
    if(!stat(fileName, &attr) && attr.st_size > 0) {
        ok = true;
        *writeTime = *(uint64_t*)&attr.st_mtim;
    }
    return ok;
}

bool CopyFile(const char *src, const char *dst)
{
    int fd_src = open(src, O_RDONLY);
    int fd_dst = open(dst, O_CREAT|O_WRONLY|O_TRUNC);

    bool ok = false;

    if(fd_src == -1 || fd_dst == -1) return false;

    struct stat attr;
    if(!fstat(fd_src, &attr)) {
        void *buf = malloc(attr.st_size);
        if(buf) {
            if(read(fd_src, buf, attr.st_size) == attr.st_size &&
               write(fd_dst, buf, attr.st_size) == attr.st_size) {
                ok = true;
            }
            free(buf);
        }
    }
    return ok;
}

bool LoadProgramApi(ProgramApi *api, int version)
{
    bool ok = false;

    if(GetLastWriteTime(DLL_NAME, &api->modificationTime)) {
        size_t mark = nob_temp_save();
        char *dllname = nob_temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, version);
        nob_temp_rewind(mark);
        if(CopyFile(DLL_NAME, dllname, FALSE)) {
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
    size_t mark = temp_buf_size;
    if(unlink(nob_temp_sprintf(DLL_DIR "/app_%d" DLL_EXT, api->version))) {
        fprintf(stderr, "Could not delete dll file");
    }
    temp_buf_size = mark;
}
#endif

typedef struct {
    ProgramApi *items;
    size_t count;
    size_t capacity;
} ProgramApis;

#if defined(_WIN32)
bool ProgramAlreadyRunning(const char *program)
{
    bool running = false;
    HANDLE mut = CreateMutexA(0, FALSE, nob_temp_sprintf("Local\\%s", program));
    if(GetLastError() == ERROR_ALREADY_EXISTS) {
        running = true;
    }
    else if(mut) CloseHandle(mut);

    return running;
}
#else
bool ProgramAlreadyRunning(const char *program)
{
    bool running = false;
    DIR* dir;
    struct dirent* ent;
    char* endptr;
    char buf[512];

    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc");
        return -1;
    }

    while((ent = readdir(dir)) != NULL) {
        /* if endptr is not a null character, the directory is not
         * entirely numeric, so ignore it */
        long lpid = strtol(ent->d_name, &endptr, 10);
        if (*endptr != '\0') {
            continue;
        }

        /* try to open the cmdline file */
        size_t mark = nob_temp_save();
        char *buf = nob_temp_sprintf("/proc/%ld/cmdline", lpid);
        nob_temp_rewind(mark);
        FILE* fp = fopen(buf, "r");

        if (fp) {
            if (fgets(buf, strlen(buf), fp) != NULL) {
                /* check the first token in the file, the program name */
                char *first = strtok(buf, " ");
                if (!strcmp(first, name)) {
                    fclose(fp);
                    closedir(dir);
                    running = true;
                }
            }
            fclose(fp);
        }
    }

    closedir(dir);
    return running;
}
#endif

int main(int argc, char **argv)
{
    (void) argc;
    if(ProgramAlreadyRunning(argv[0])) return 0;
    char *path = nob_get_executable_dir_temp();
    nob_set_current_dir(path);

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
                    nob_da_append(&oldApis, api);
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
    NOB_FREE(oldApis.items);

    return 0;
}

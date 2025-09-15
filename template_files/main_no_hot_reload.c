#define DLL_EXPORT static
#include "app.c"

#define NOB_IMPLEMENTATION
#include "../nob.h"

#if defined(_WIN32)
#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
bool ProgramAlreadyRunning(const char *program)
{
    bool running = false;
    DIR* dir;
    struct dirent* ent;
    char* endptr;
    char buf[512];

    if (!(dir = opendir("/proc"))) {
        return false;
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

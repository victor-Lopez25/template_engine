#define VICLIB_PATH "src/viclib.h"
#define VL_BUILD_IMPLEMENTATION
#include "vl_build.h"

#ifndef OUT_DIRECTORY
# define OUT_DIRECTORY "bin"
#endif

#if defined(_WIN32)
#include <tlhelp32.h>
bool ProgramAlreadyRunning(const char *program)
{
    bool running = false;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE) {
        VL_Log(VL_ERROR, "Could not enum processes: %s", Win32_ErrorMessage(GetLastError()));
        return false;
    }

    PROCESSENTRY32 processInfo = {
        .dwSize = sizeof(PROCESSENTRY32),
    };
    if(!Process32First(snapshot, &processInfo)) {
        VL_Log(VL_ERROR, "Could not get process info: %s", Win32_ErrorMessage(GetLastError()));
        CloseHandle(snapshot);
        return false;
    }
    do {
        if(!strcmp(&processInfo.szExeFile[0], program)) {
            running = true;
            break;
        }
    } while(Process32Next(snapshot, &processInfo));

    CloseHandle(snapshot);
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

    if(!(dir = opendir("/proc"))) {
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
        size_t mark = temp_save();
        char *buf = temp_sprintf("/proc/%ld/cmdline", lpid);
        FILE* fp = fopen(buf, "r");
        temp_rewind(mark);

        if(fp) {
            if(fgets(buf, strlen(buf), fp) != NULL) {
                /* check the first token in the file, the program name */
                char *first = strtok(buf, " ");
                if(!strcmp(first, name)) {
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

vl_compile_ctx app_ctx = {
    .type = Compile_DynamicLibrary,
    .debug = false,
    .gcSections = true,
    .warnings = true,
    .includePaths = VL_GetDaStrSlice("../include"),
#if defined(_WIN32)
    .libPaths = VL_GetDaStrSlice("../lib"),
#endif
    .libs = VL_GetDaStrSlice("SDL3", "SDL3_ttf", "SDL3_image"),
};

bool CompileApp(vl_cmd *cmd)
{
    app_ctx.type = Compile_DynamicLibrary;
    app_ctx.sourceFiles = (vl_file_paths)VL_GetDaStrSlice("../src/app.c");
    app_ctx.outputPath = "app";
    VL_SetupCCompile(cmd, &app_ctx);
#if defined(_MSC_VER)
    CmdAppend(cmd, "/subsystem:console");
#endif

#if defined(_MSC_VER)
# define LOCK_FILE_NAME "lock.tmp"
    char pdb_lock_str[] = "PDBSHIT";
    WriteEntireFile(LOCK_FILE_NAME, pdb_lock_str, sizeof(pdb_lock_str));
#endif

    if(!CmdRun(cmd)) return false;

#if defined(_MSC_VER)
    VL_DeleteFile(LOCK_FILE_NAME);
#endif

    return true;
}

int main(int argc, char **argv)
{
    VL_GO_REBUILD_URSELF(argc, argv);

#define EXE_NAME "template"

    bool hotreload = true;
    bool shouldrun = true;
    bool warningsAsErrors = false;

    while(argc > 1) {
        char *arg = argv[--argc];
        if(!strcmp(arg, "hotreload")) {
            hotreload = true;
        } else if(!strcmp(arg, "nohotreload")) {
            hotreload = false;
        } else if(!strcmp(arg, "norun")) {
            shouldrun = false;
        } else if(!strcmp(arg, "test")) {
            shouldrun = false;
            warningsAsErrors = true;
        }
    }
    app_ctx.warningsAsErrors = warningsAsErrors;

    VL_CopyDirectoryRecursively("dependencies", "bin");
    VL_Pushd("bin");

    vl_cmd cmd = {0};
    if(!CompileApp(&cmd)) return 1;

    if(hotreload) {
        MkdirIfNotExist("hotreload");
        // Done since we're not going to rerun the program
        if(ProgramAlreadyRunning(EXE_NAME VL_EXE_EXTENSION)) return 0;

        vl_compile_ctx ctx = {
            .debug = false,
            .gcSections = true,
            .warnings = true,
            .warningsAsErrors = warningsAsErrors,
            .sourceFiles = VL_GetDaStrSlice("../src/main_hot_reload.c"),
            .outputPath = EXE_NAME,
        };
        VL_SetupCCompile(&cmd, &ctx);
        if(!CmdRun(&cmd)) return 1;
    } else {
        app_ctx.type = Compile_Executable;
        app_ctx.outputPath = EXE_NAME;
        app_ctx.sourceFiles = (vl_file_paths)VL_GetDaStrSlice("../src/main_no_hot_reload.c");
        VL_SetupCCompile(&cmd, &app_ctx);
        if(!CmdRun(&cmd)) return 1;
    }

    if(shouldrun) {
        CmdAppend(&cmd, "./" EXE_NAME);
        if(!CmdRun(&cmd)) {
            VL_Popd();
            return 1;
        }
    }

    VL_Popd();

    return 0;
}

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

bool CompileApp(vl_cmd *cmd, bool warningsAsErrors)
{
    VL_cc(cmd);
    cmd_Append(cmd, "../src/app.c", "-I", "../include", "-DSHADER_DIRECTORY=\"../shaders/\"");
    VL_ccOutput(cmd, "app" VL_DLL_EXT);
    VL_ccWarnings(cmd);
    if(warningsAsErrors) VL_ccWarningsAsErrors(cmd);

#if defined(_WIN32)
    VL_ccLibpath(cmd, "../lib"); // also appends "/link" with msvc
#endif

#if defined(_MSC_VER)
    cmd_Append(cmd, "/DLL", "-incremental:no", "-opt:ref", "/subsystem:console");
#else
    cmd_Append(cmd, "-shared");
#endif

    VL_ccLibs(cmd, "SDL3", "SDL3_ttf", "SDL3_image", "SDL3_shadercross");

#if defined(_MSC_VER)
# define LOCK_FILE_NAME "lock.tmp"
    char pdb_lock_str[] = "PDBSHIT";
    VL_WriteEntireFile(LOCK_FILE_NAME, pdb_lock_str, sizeof(pdb_lock_str));
#endif
    if(!CmdRun(cmd)) return false;
#if defined(_MSC_VER)
    VL_DeleteFile(LOCK_FILE_NAME);
#endif

    return true;
}

// NOTE: For glslc
char *GetShaderstageFromExt(view file) {
    if(view_EndWith(file, VIEW_STATIC(".vert"))) {
        return "vert"; // 'vertex' is also valid
    } if(view_EndWith(file, VIEW_STATIC(".frag"))) {
        return "frag"; // 'fragment' is also valid
    } if(view_EndWith(file, VIEW_STATIC(".comp"))) {
        return "comp"; // 'compute' is also valid
    } else return 0; // SDL_gpu doesn't support others If I understand correctly
}

bool CompileGlslShader(vl_cmd *cmd, view shaderfile)
{
    // NOTE: To compile glsl shaders, glslc is required
    view noGlslExt = view_FromParts(shaderfile.items, shaderfile.count - strlen(".glsl"));
    char *output = temp_sprintf(VIEW_FMT".spv", VIEW_ARG(noGlslExt));
    if(VL_NeedsRebuild(output, shaderfile.items) == 1) {
        char *shaderStage = GetShaderstageFromExt(noGlslExt);
        if(!shaderStage) {
            VL_Log(VL_ERROR, "Invalid extension in shader file referencing shader stage '"VIEW_FMT"'", VIEW_ARG(shaderfile));
            return false;
        }
        cmd_Append(cmd, "glslc", "-o", output, 
            temp_sprintf("-fshader-stage=%s", shaderStage), shaderfile.items);
        if(!CmdRun(cmd)) {
            VL_Log(VL_ERROR, "Could not compile "VIEW_FMT" to %s", VIEW_ARG(noGlslExt), output);
            return false;
        }
    }
    return true;
}

void CompileGlslShadersInDirectory(vl_cmd *cmd, const char *directory)
{
    vl_file_paths files = {0};
    if(VL_ReadEntireDir(directory, &files)) {
        for(size_t fileIdx = 0; fileIdx < files.count; fileIdx++) {
            const char *file = temp_sprintf("%s/%s", directory, files.items[fileIdx]);
            view fileView = view_FromCstr(file);
            if(VL_GetFileType(file) == VL_FILE_REGULAR && 
               view_EndWith(fileView, VIEW_STATIC(".glsl")))
            {
                CompileGlslShader(cmd, fileView);
            }
        }
    }
    VL_FREE(files.items);
}

int main(int argc, char **argv)
{
    //VL_GO_REBUILD_URSELF(argc, argv);

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

    VL_CopyDirectoryRecursively("dependencies", "bin");
    VL_Pushd("bin");
    VL_CopyDirectoryRecursively("../resources", "resources");

    vl_cmd cmd = {0};
    if(!CompileApp(&cmd, warningsAsErrors)) return 1;

    CompileGlslShadersInDirectory(&cmd, "../shaders");
    VL_CopyDirectoryRecursively("../shaders", .dst = "shaders", .ext = ".hlsl");
    VL_CopyDirectoryRecursively("../shaders", .dst = "shaders", .ext = ".glsl");
    VL_CopyDirectoryRecursively("../shaders", .dst = "shaders", .ext = ".spv");

    if(hotreload) {
        MkdirIfNotExist("hotreload");
        // Done since we're not going to rerun the program
        if(ProgramAlreadyRunning(EXE_NAME VL_EXE_EXTENSION)) return 0;
        VL_cc(&cmd);
        cmd_Append(&cmd, "../src/main_hot_reload.c");
        VL_ccOutput(&cmd, EXE_NAME VL_EXE_EXTENSION);
        VL_ccWarnings(&cmd);
        if(warningsAsErrors) VL_ccWarningsAsErrors(&cmd);
#if defined(_MSC_VER)
        cmd_Append(&cmd, "/link", "-incremental:no", "-opt:ref");
#endif
        if(!CmdRun(&cmd)) return 1;
    } else {
        VL_cc(&cmd);
        cmd_Append(&cmd, "../src/main_no_hot_reload.c", "-I", "../include", "-DSHADER_DIRECTORY=\"shaders/\"");
        VL_ccOutput(&cmd, EXE_NAME VL_EXE_EXTENSION);
        VL_ccWarnings(&cmd);
        if(warningsAsErrors) VL_ccWarningsAsErrors(&cmd);
        VL_ccLibs(&cmd, "SDL3", "SDL3_ttf", "SDL3_image", "SDL3_shadercross");
#if defined(_WIN32)
        VL_ccLibpath(&cmd, "../lib");
#endif
#if defined(_MSC_VER)
        cmd_Append(&cmd, "-incremental:no", "-opt:ref");
#endif
        if(!CmdRun(&cmd)) return 1;
    }

    if(shouldrun) {
        cmd_Append(&cmd, "./" EXE_NAME);
        if(!CmdRun(&cmd)) {
            VL_Popd();
            return 1;
        }
    }

    VL_Popd();

    return 0;
}

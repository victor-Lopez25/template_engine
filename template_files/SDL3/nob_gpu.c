#if defined(_WIN32)
# if defined(__GNUC__)
#  define COMMON_FLAGS "-Wall", "-Wextra"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "gcc", "-o", binary_path, source_path, "-Wall", "-Wextra"
# elif defined(__clang__)
#  define COMMON_FLAGS "-Wall", "-Wextra"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "clang", "-o", binary_path, source_path, "-Wall", "-Wextra"
# elif defined(_MSC_VER)
#  define COMMON_FLAGS "/nologo", "-Zi", "-FC", "-GR-", "-EHa", "-W4"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "cl.exe", nob_temp_sprintf("/Fe:%s", (binary_path)), source_path, COMMON_FLAGS, "/link", "-incremental:no"
#  define LOCK_FILE_NAME "lock.tmp"
# endif
#else
# define COMMON_FLAGS "-Wall", "-Wextra"
# define NOB_REBUILD_URSELF(binary_path, source_path) "cc", "-o", binary_path, source_path, COMMON_FLAGS
#endif

#define NOB_IMPLEMENTATION
#include "nob.h"

#ifndef OUT_DIRECTORY
# define OUT_DIRECTORY "bin"
#endif

#if defined(_WIN32)
# define DLL_EXT ".dll"
#elif __APPLE__
# define DLL_EXT ".dynlib"
#else
# define DLL_EXT ".so"
#endif

#define cmd_cc_libs(cmd, ...) \
    cmd_cc_libs_(cmd, \
               ((const char*[]){__VA_ARGS__}), \
                (sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*)))

void cmd_cc_libs_(Nob_Cmd *cmd, const char **items, size_t itemCount)
{
    for(size_t i = 0; i < itemCount; i++) {
#if defined(_MSC_VER)
        nob_cmd_append(cmd, nob_temp_sprintf("%s.lib", items[i]));
#else
        nob_cmd_append(cmd, "-l", items[i]);
#endif
    }
}

void cmd_cc_libpath(Nob_Cmd *cmd, const char *path)
{
#if defined(_MSC_VER)
    nob_cmd_append(cmd, nob_temp_sprintf("/libpath:%s", path));
#else
    nob_cmd_append(cmd, "-L", path);
#endif
}

#if defined(_WIN32)
#include <tlhelp32.h>
bool ProgramAlreadyRunning(const char *program)
{
    bool running = false;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE) {
        nob_log(NOB_ERROR, "Could not enum processes: %s", nob_win32_error_message(GetLastError()));
        return false;
    }

    PROCESSENTRY32 processInfo = {
        .dwSize = sizeof(PROCESSENTRY32),
    };
    if(!Process32First(snapshot, &processInfo)) {
        nob_log(NOB_ERROR, "Could not get process info: %s", nob_win32_error_message(GetLastError()));
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

bool CompileApp(Nob_Cmd *cmd, bool warningsAsErrors)
{
    nob_cc(cmd);
    nob_cmd_append(cmd, "../src/app.c", COMMON_FLAGS, "-I", "../include", "-DSHADER_DIRECTORY=\"../shaders/\"");
    if(warningsAsErrors) nob_cc_warnings_as_errors(cmd);
#if defined(_MSC_VER)
    nob_cmd_append(cmd, nob_temp_sprintf("/Fe:%s", "app" DLL_EXT), "-D_CRT_SECURE_NO_WARNINGS", "/link", "/DLL", "-incremental:no", "-opt:ref", "/subsystem:console");
#else
    nob_cmd_append(cmd, "-o", "app" DLL_EXT, "-shared");
#endif
#if defined(_WIN32)
    cmd_cc_libpath(cmd, "../lib");
#endif
    cmd_cc_libs(cmd, "SDL3", "SDL3_ttf", "SDL3_image", "SDL3_shadercross");

#if defined(_MSC_VER)
    char pdb_lock_str[] = "PDBSHIT";
    nob_write_entire_file(LOCK_FILE_NAME, pdb_lock_str, sizeof
    (pdb_lock_str));
#endif
    if(!nob_cmd_run(cmd)) return false;
#if defined(_MSC_VER)
    nob_delete_file(LOCK_FILE_NAME);
#endif

    return true;
}

// NOTE: For glslc
char *GetShaderstageFromExt(Nob_String_View file) {
    if(nob_sv_end_with(file, ".vert")) {
        return "vert"; // 'vertex' is also valid
    } if(nob_sv_end_with(file, ".frag")) {
        return "frag"; // 'fragment' is also valid
    } if(nob_sv_end_with(file, ".comp")) {
        return "comp"; // 'compute' is also valid
    } else return 0; // SDL_gpu doesn't support others If I understand correctly
}

bool CompileGlslShader(Nob_Cmd *cmd, Nob_String_View shaderfile)
{
    // NOTE: To compile glsl shaders, glslc is required
    Nob_String_View noGlslExt = nob_sv_from_parts(shaderfile.data, shaderfile.count - strlen(".glsl"));
    char *output = nob_temp_sprintf(SV_Fmt".spv", SV_Arg(noGlslExt));
    if(nob_needs_rebuild1(output, shaderfile.data) == 1) {
        char *shaderStage = GetShaderstageFromExt(noGlslExt);
        if(!shaderStage) {
            nob_log(NOB_ERROR, "Invalid extension in shader file referencing shader stage '"SV_Fmt"'", SV_Arg(shaderfile));
            return false;
        }
        nob_cmd_append(cmd, "glslc", "-o", output, 
            nob_temp_sprintf("-fshader-stage=%s", shaderStage), shaderfile.data);
        if(!nob_cmd_run(cmd)) {
            nob_log(NOB_ERROR, "Could not compile "SV_Fmt" to %s", SV_Arg(noGlslExt), output);
            return false;
        }
    }
    return true;
}

void CompileGlslShadersInDirectory(Nob_Cmd *cmd, const char *directory)
{
    Nob_File_Paths files = {0};
    if(nob_read_entire_dir(directory, &files)) {
        for(size_t fileIdx = 0; fileIdx < files.count; fileIdx++) {
            const char *file = nob_temp_sprintf("%s/%s", directory, files.items[fileIdx]);
            Nob_String_View fileView = nob_sv_from_cstr(file);
            if(nob_get_file_type(file) == NOB_FILE_REGULAR && 
               nob_sv_end_with(fileView, ".glsl"))
            {
                CompileGlslShader(cmd, fileView);
            }
        }
    }
    NOB_FREE(files.items);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *app_name = "template";

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

    nob_copy_directory_recursively("dependencies", "bin");
    nob_set_current_dir("bin");
    nob_copy_directory_recursively("../resources", "resources");
    Nob_Cmd cmd = {0};

    if(!CompileApp(&cmd, warningsAsErrors)) return 1;

    CompileGlslShadersInDirectory(&cmd, "../shaders");
    nob_copy_directory_recursively("../shaders", .dst = "shaders", .ext = ".hlsl");
    nob_copy_directory_recursively("../shaders", .dst = "shaders", .ext = ".spv");

#if defined(_WIN32)
    app_name = nob_temp_sprintf("%s.exe", app_name);
#endif

    if(hotreload) {
        if(ProgramAlreadyRunning(app_name)) return 0; // Done since we're not going to rerun the program
        nob_mkdir_if_not_exists("hotreload");

        nob_cc(&cmd);
        nob_cc_output(&cmd, app_name);
        nob_cmd_append(&cmd, "../src/main_hot_reload.c", COMMON_FLAGS);
        if(warningsAsErrors) nob_cc_warnings_as_errors(&cmd);
#if defined(_MSC_VER)
        nob_cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS", "/link", "-incremental:no", "-opt:ref");
#endif
        if(!nob_cmd_run(&cmd)) return 1;
    } else {
        nob_cc(&cmd);
        nob_cc_output(&cmd, app_name);
        nob_cmd_append(&cmd, "../src/main_no_hot_reload.c", COMMON_FLAGS, "-I", "../include", "-DSHADER_DIRECTORY=\"shaders/\"");
        if(warningsAsErrors) nob_cc_warnings_as_errors(&cmd);
#if defined(_MSC_VER)
        nob_cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS", "/link", "-incremental:no", "-opt:ref");
#endif
        cmd_cc_libpath(&cmd, "../lib");
        cmd_cc_libs(&cmd, "SDL3", "SDL3_ttf", "SDL3_image", "SDL3_shadercross");

        if(!nob_cmd_run(&cmd)) return 1;
    }

    if(shouldrun) {
        nob_cmd_append(&cmd, nob_temp_sprintf("./%s", app_name));
        if(!nob_cmd_run(&cmd)) return 1;
    }

    nob_set_current_dir("..");

    return 0;
}
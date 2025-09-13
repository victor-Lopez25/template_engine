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

bool CompileApp(Nob_Cmd *cmd)
{
    nob_cc(cmd);
    nob_cmd_append(cmd, "../src/app.c", COMMON_FLAGS, "-I", "../include");
#if defined(_MSC_VER)
    nob_cmd_append(cmd, nob_temp_sprintf("/Fe:%s", "app" DLL_EXT), "-D_CRT_SECURE_NO_WARNINGS", "/link", "/DLL", "-incremental:no", "-opt:ref", "/subsystem:console");
#else
    nob_cmd_append(cmd, "-o", "app" DLL_EXT, "-shared");
#endif
    cmd_cc_libpath(cmd, "../lib");
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

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *app_name = "template";

    bool hotreload = true;
    bool shouldrun = true;

    while(argc > 1) {
        char *arg = argv[--argc];
        if(!strcmp(arg, "hotreload")) {
            hotreload = true;
        } else if(!strcmp(arg, "nohotreload")) {
            hotreload = false;
        } else if(!strcmp(arg, "norun")) {
            shouldrun = false;
        }
    }

    nob_copy_directory_recursively("dependencies", "bin");
    nob_set_current_dir("bin");
    Nob_Cmd cmd = {0};

    if(!CompileApp(&cmd)) return 1;

    if(hotreload) {
        nob_mkdir_if_not_exists("hotreload");
        if(!ProgramAlreadyRunning(app_name)) {
            nob_cc(&cmd);
            nob_cc_output(&cmd, app_name);
            nob_cmd_append(&cmd, "../src/main_hot_reload.c", COMMON_FLAGS);
#if defined(_MSC_VER)
            nob_cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS", "/link", "-incremental:no", "-opt:ref");
#endif
            if(!nob_cmd_run(&cmd)) return 1;
        }
    } else {
        nob_cc(&cmd);
        nob_cc_output(&cmd, app_name);
        nob_cmd_append(&cmd, "../src/main_no_hot_reload.c", COMMON_FLAGS, "-I", "../include");
#if defined(_MSC_VER)
        nob_cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS", "/link", "-incremental:no", "-opt:ref");
#endif
        cmd_cc_libpath(&cmd, "../lib");
        cmd_cc_libs(&cmd, "SDL3", "SDL3_ttf", "SDL3_image", "SDL3_shadercross");

        if(!nob_cmd_run(&cmd)) return 1;
    }

    if(shouldrun) {
#if defined(_WIN32)
        nob_cmd_append(&cmd, nob_temp_sprintf("%s.exe", app_name));
#else
        nob_cmd_append(&cmd, app_name);
#endif
        if(!nob_cmd_run(&cmd)) return 1;
    }

    nob_set_current_dir("..");

    return 0;
}
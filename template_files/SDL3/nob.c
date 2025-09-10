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

#define cmd_cc_libs(cmd, ...) \
    cmd_cc_libs_(cmd, \
               ((const char*[]){__VA_ARGS__}), \
                (sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*)))

void cmd_cc_libs_(Nob_Cmd *cmd, const char **items, size_t itemCount)
{
    for(size_t i = 0; i < itemCount; i++) {
#if defined(_MSC_VER)
        nob_cmd_append(cmd, items[i]);
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

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    bool shouldrun = true;

    while(argc > 1) {
        char *arg = argv[--argc];
        if(!strcmp(arg, "norun")) {
            shouldrun = false;
        }
    }

    Nob_Cmd cmd = {0};
    nob_copy_directory_recursively("dependencies", "bin");
    nob_cc(&cmd);
    nob_cmd_append(&cmd, "../src/main.c", COMMON_FLAGS, "-I", "../include");
    nob_cc_output(&cmd, "template");
#if defined(_MSC_VER)
    nob_cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS", "/link", "-incremental:no", "-opt:ref", "/subsystem:console");
#endif
    cmd_cc_libpath(&cmd, "../lib");
    cmd_cc_libs(&cmd, "SDL3.lib", "SDL3_ttf.lib", "SDL3_image.lib");

    nob_mkdir_if_not_exists("bin");
    nob_set_current_dir("bin");
    if(!nob_cmd_run(&cmd)) return 1;

    if(shouldrun) {
        nob_cmd_append(&cmd, "template");
        if(!nob_cmd_run(&cmd)) return 1;
    }
    nob_set_current_dir("..");

    return 0;
}
#define VICLIB_PATH "src/viclib.h"
#define VL_BUILD_IMPLEMENTATION
#include "vl_build.h"

#ifndef OUT_DIRECTORY
# define OUT_DIRECTORY "bin"
#endif

int main(int argc, char **argv)
{
    VL_GO_REBUILD_URSELF(argc, argv);

    bool shouldrun = true;
    bool warningsAsErrors = false;

    while(argc > 1) {
        char *arg = argv[--argc];
        if(!strcmp(arg, "norun")) {
            shouldrun = false;
        } else if(!strcmp(arg, "test")) {
            shouldrun = false;
            warningsAsErrors = true;
        }
    }

    vl_cmd cmd = {0};
    VL_CopyDirectoryRecursively("dependencies", "bin");
    VL_cc(&cmd);
    cmd_Append(&cmd, "../src/main.c", "-I", "../include");
    VL_ccOutput(&cmd, "template" VL_EXE_EXTENSION);
    VL_ccWarnings(&cmd);
    if(warningsAsErrors) VL_ccWarningsAsErrors(&cmd);

#if defined(_WIN32)
    VL_ccLibpath(&cmd, "../lib");
#endif
#if defined(_MSC_VER)
    // link flags
    cmd_Append(&cmd, "-incremental:no", "-opt:ref", "/subsystem:console");
#endif
    VL_ccLibs(&cmd, "SDL3", "SDL3_ttf", "SDL3_image");

    MkdirIfNotExist("bin");
    VL_Pushd("bin");
    if(!CmdRun(&cmd)) return 1;

    if(shouldrun) {
        cmd_Append(&cmd, "./template");
        if(!CmdRun(&cmd)) return 1;
    }
    VL_Popd();

    return 0;
}

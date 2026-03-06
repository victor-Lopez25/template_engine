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

    vl_compile_ctx ctx = {
        .debug = false,
        .gcSections = true,
        .warnings = true,
        .warningsAsErrors = warningsAsErrors,
        .sourceFiles = VL_GetDaStrSlice("../src/main.c"),
        .outputPath = "template",
        .includePaths = VL_GetDaStrSlice("../include"),
#if defined(_WIN32)
        .libPaths = VL_GetDaStrSlice("../lib"),
#endif
        .libs = VL_GetDaStrSlice("SDL3", "SDL3_ttf", "SDL3_image"),
    };

    VL_SetupCCompile(&cmd, &ctx);
#if defined(_MSC_VER)
    CmdAppend(&cmd, "/subsystem:console");
#endif

    MkdirIfNotExist("bin");
    VL_Pushd("bin");
    if(!CmdRun(&cmd)) return 1;

    if(shouldrun) {
        CmdAppend(&cmd, "./template");
        if(!CmdRun(&cmd)) return 1;
    }
    VL_Popd();

    return 0;
}

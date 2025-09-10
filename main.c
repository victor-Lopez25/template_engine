/* Copyright 2025 Víctor López Cortés <victorlopezcortes25@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

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

#if defined(_WIN32)
char selfPath[MAX_PATH];
#else
char selfPath[PATH_MAX];
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

char *GetSelfPath() {
#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(0, selfPath, MAX_PATH);
    if(!len) {
        return 0;
    }
#elif defined(__APPLE__)
    uint32_t bufsize = PATH_MAX;
    if(_NSGetExecutablePath(selfPath, &bufsize)) {
        return 0;
    }
#else
    ssize_t len = readlink("/proc/self/exe", selfPath, PATH_MAX);
    if(len == -1) return 0;
    selfPath[len] = 0;
#endif
    while(len > 0 && selfPath[len] != '/' && selfPath[len] != '\\') len--;
    selfPath[len] = 0;

    return selfPath;
}

void Usage(char *program)
{
    printf("Usage: %s <template name>\n"
           "Possible templates are:\n"
           " - SDL3\n"
           "\n"
           "This program will make a template program in the current directory\n",
           program);
}

typedef enum {
    Template_None,
    Template_SDL3,
} Template;

Template chosenTemplate = Template_None;

int main(int argc, char **argv)
{
    bool inExeDirectory = !strcmp(GetSelfPath(), nob_get_current_dir_temp());
    if(inExeDirectory) {
        NOB_GO_REBUILD_URSELF(argc, argv);
    }
    nob_temp_reset();

    for(int argIdx = 1; argIdx < argc; argIdx++) {
        char *arg = argv[argIdx];
        if(!strcmp(arg, "-h") || !strcmp(arg, "--help") || !strcmp(arg, "/?")) {
            Usage(*argv);
            return 0;
        } else if(!strcmp(arg, "SDL3")) {
            chosenTemplate = Template_SDL3;
        }
    }

    if(chosenTemplate != Template_None && inExeDirectory) {
        fprintf(stderr, "This tool is not supposed to run in the same working directory as the executable.\n");
        return 1;
    }

    switch(chosenTemplate) {
        case Template_None: {
            printf("Please choose a template\n");
            Usage(*argv);
        } break;

        case Template_SDL3: {
            printf("Chosen template: SDL3\n");
            
            nob_mkdir_if_not_exists("src");
            nob_mkdir_if_not_exists("lib");
            nob_mkdir_if_not_exists("include");
            nob_mkdir_if_not_exists("dependencies");

            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/bin/SDL3.dll", selfPath), "dependencies/SDL3.dll");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/bin/SDL3_image.dll", selfPath), "dependencies/SDL3_image.dll");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/bin/SDL3_ttf.dll", selfPath), "dependencies/SDL3_ttf.dll");
            // SDL3_mixer also?
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/lib/SDL3.lib", selfPath), "lib/SDL3.lib");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/lib/SDL3_image.lib", selfPath), "lib/SDL3_image.lib");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/lib/SDL3_ttf.lib", selfPath), "lib/SDL3_ttf.lib");

            nob_copy_directory_recursively(nob_temp_sprintf("%s/template_files/SDL3/include", selfPath), "include/SDL3");

            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/main.c", selfPath), "src/main.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/spall.h", selfPath), "src/spall.h");

            nob_copy_file(nob_temp_sprintf("%s/nob.h", selfPath), "nob.h");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/nob.c", selfPath), "nob.c");
        } break;
    }

    return 0;

}

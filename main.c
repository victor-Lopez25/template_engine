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
#  define COMMON_FLAGS_NODEBUG "-Wall", "-Wextra"
#  define COMMON_FLAGS COMMON_FLAGS_NODEBUG, "-g"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "gcc", "-o", binary_path, source_path, "-Wall", "-Wextra"
# elif defined(__clang__)
#  define COMMON_FLAGS_NODEBUG "-Wall", "-Wextra"
#  define COMMON_FLAGS COMMON_FLAGS_NODEBUG, "-g"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "clang", "-o", binary_path, source_path, "-Wall", "-Wextra"
# elif defined(_MSC_VER)
#  define COMMON_FLAGS_NODEBUG "/nologo", "-FC", "-GR-", "-EHa", "-W4"
#  define COMMON_FLAGS COMMON_FLAGS_NODEBUG, "-Zi"
#  define NOB_REBUILD_URSELF(binary_path, source_path) "cl.exe", nob_temp_sprintf("/Fe:%s", (binary_path)), source_path, COMMON_FLAGS, "/link", "-incremental:no"
# endif
#else
# define COMMON_FLAGS_NODEBUG "-Wall", "-Wextra"
# define COMMON_FLAGS COMMON_FLAGS_NODEBUG, "-g"
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
           "Not currently working for linux, will fix soon\n"
           "Possible templates are:\n"
           " - SDL3\n"
           " - SDL3-hotreload\n"
           " - SDL3-gpu (wip)\n"
           "\n"
           "This program will make a template program in the current directory\n",
           program);
}

typedef enum {
    Template_None,
    Template_SDL3,
    Template_SDL3_Hotreload,
    Template_SDL3_GPU_Hotreload,
    Count_Templates,
} Template;

const char *TemplateToString(Template t)
{
    switch(t) {
        case Template_SDL3: return "SDL3";
        case Template_SDL3_Hotreload: return "SDL3 hotreload";
        case Template_SDL3_GPU_Hotreload: return "SDL3_GPU hotreload";
        default: return "Unknown";
    }
}

Template chosenTemplate = Template_None;

#define SetupGeneralSDL3Templates(...) \
    SetupGeneralSDL3Templates_(((const char*[]){"SDL3", __VA_ARGS__}), \
                               (sizeof((const char*[]){"SDL3", __VA_ARGS__})/sizeof(const char*)))
void SetupGeneralSDL3Templates_(const char **libs, size_t libcount)
{
    nob_mkdir_if_not_exists("src");
    nob_mkdir_if_not_exists("lib");
    nob_mkdir_if_not_exists("include");
    nob_mkdir_if_not_exists("dependencies");

    size_t mark = nob_temp_save();

    for(size_t i = 0; i < libcount; i++) {
        nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/bin/%s.dll", selfPath, libs[i]), nob_temp_sprintf("dependencies/%s.dll", libs[i]));
        nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/lib/%s.lib", selfPath, libs[i]), nob_temp_sprintf("lib/%s.lib", libs[i]));
    }

    nob_copy_directory_recursively(nob_temp_sprintf("%s/template_files/SDL3/include", selfPath), "include/SDL3");

    nob_copy_file(nob_temp_sprintf("%s/template_files/spall.h", selfPath), "src/spall.h");
    nob_copy_file(nob_temp_sprintf("%s/nob.h", selfPath), "nob.h");

    nob_temp_rewind(mark);
}

void DoTemplate(Template chosen)
{
    NOB_ASSERT(chosen > Template_None || chosen < Count_Templates);

    nob_log(NOB_INFO, "Chosen template: %s\n", TemplateToString(chosen));
    switch(chosen) {
        case Template_SDL3: {
            // SDL3_mixer also?
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf");

            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/main.c", selfPath), "src/main.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/nob.c", selfPath), "nob.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/README.md", selfPath), "README.md");
        } break;

        case Template_SDL3_Hotreload: {
            // SDL3_mixer also?
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf");

            nob_copy_file(nob_temp_sprintf("%s/template_files/main_hot_reload.c", selfPath), "src/main_hot_reload.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/main_no_hot_reload.c", selfPath), "src/main_no_hot_reload.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/app.c", selfPath), "src/app.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/nob_hotreload.c", selfPath), "nob.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/README_hotreload.md", selfPath), "README.md");
        } break;

        case Template_SDL3_GPU_Hotreload: {
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf", "SDL3_shadercross");

            nob_copy_file(nob_temp_sprintf("%s/template_files/main_hot_reload.c", selfPath), "src/main_hot_reload.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/main_no_hot_reload.c", selfPath), "src/main_no_hot_reload.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/app_gpu.c", selfPath), "src/app.c");
            nob_copy_file(nob_temp_sprintf("%s/template_files/SDL3/nob_gpu.c", selfPath), "nob.c");
        } break;

        case Template_None: case Count_Templates: break;
    }
}

bool TestTemplate(Nob_Cmd *cmd, Template chosen)
{
    NOB_ASSERT(chosen > Template_None || chosen < Count_Templates);

    switch(chosen) {
        case Template_SDL3: {
            nob_cc(cmd);
            nob_cc_output(cmd, "nob");
            nob_cmd_append(cmd, "nob.c", COMMON_FLAGS_NODEBUG);
            if(!nob_cmd_run(cmd)) return false;
            nob_cmd_append(cmd, "nob", "norun");
            if(!nob_cmd_run(cmd)) return false;
        } break;

        case Template_SDL3_Hotreload:
        case Template_SDL3_GPU_Hotreload: {
            nob_cc(cmd);
            nob_cc_output(cmd, "nob");
            nob_cmd_append(cmd, "nob.c", COMMON_FLAGS_NODEBUG);
            if(!nob_cmd_run(cmd)) return false;
            nob_cmd_append(cmd, "nob", "norun", "hotreload");
            if(!nob_cmd_run(cmd)) return false;

            nob_cmd_append(cmd, "nob", "norun", "nohotreload");
            if(!nob_cmd_run(cmd)) return false;
        } break;

        case Template_None: case Count_Templates: return false;
    }

    return true;
}

int IsDirectory(const char *file)
{
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(file);
    if(attr == INVALID_FILE_ATTRIBUTES || attr & FILE_ATTRIBUTE_SYSTEM) return 0;
    if(attr & FILE_ATTRIBUTE_DIRECTORY) return 1;
    return -1;
#else
    struct stat s;
    if(!stat(file, &s)) {
        if(s.st_mode & S_IFDIR) return 1;
        if(s.st_mode & S_IFREG) return -1;
        return 0;
    }
    return 0;
#endif
}

// TODO: Do this iterative maybe?
bool RemoveDirectoryRecursive(const char *dir)
{
    int attr = IsDirectory(dir);
    if(attr == 1) { // directory
        Nob_File_Paths paths = {0};
        size_t mark = nob_temp_save();
        nob_read_entire_dir(dir, &paths);
        for(size_t pathIdx = 0; pathIdx < paths.count; pathIdx++)
        {
            if(strcmp(paths.items[pathIdx], ".") && strcmp(paths.items[pathIdx], "..")) {
                if(!RemoveDirectoryRecursive(nob_temp_sprintf("%s/%s", dir, paths.items[pathIdx]))) {
                    NOB_FREE(paths.items);
                    nob_temp_rewind(mark);
                    return false;
                }
            }
        }
        NOB_FREE(paths.items);
        nob_temp_rewind(mark);
#if defined(_WIN32)
        return RemoveDirectoryA(dir);
#else
        return rmdir(dir) == 0;
#endif
    } else if(attr == -1) { // normal file
        return nob_delete_file(dir);
    }
    return false;
}

void Test(void)
{
    Nob_Cmd cmd = {0};
    bool returnOnFirstFail = true;
    nob_minimal_log_level = NOB_ERROR;
    RemoveDirectoryRecursive("temp");
    for(int templateIdx = Template_None+1; templateIdx < Count_Templates; templateIdx++)
    {
        //nob_minimal_log_level = NOB_ERROR;
        nob_mkdir_if_not_exists("temp");
        nob_set_current_dir("temp");

        DoTemplate((Template)templateIdx);
        nob_minimal_log_level = NOB_INFO;

        bool ok = TestTemplate(&cmd, (Template)templateIdx);
        nob_minimal_log_level = NOB_ERROR;

        printf("Test: %s - %s\n", TemplateToString((Template)templateIdx), ok ? "success" : "fail");
        nob_set_current_dir("..");
        if(returnOnFirstFail && !ok) return;
        if(!RemoveDirectoryRecursive("temp")) return;
    }
}

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
        } else if(!strcmp(arg, "SDL3-hotreload")) {
            chosenTemplate = Template_SDL3_Hotreload;
        } else if(!strcmp(arg, "SDL3-gpu")) {
            chosenTemplate = Template_SDL3_GPU_Hotreload;
        } else if(!strcmp(arg, "test")) {
            Test();
            return 0;
        }
    }

    if(chosenTemplate == Template_None) {
        printf("Please choose a template\n");
        Usage(*argv);
        return 0;
    } else if(inExeDirectory) {
        fprintf(stderr, "This tool is not supposed to run in the same working directory as the executable.\n");
        return 1;
    }

    DoTemplate(chosenTemplate);

    return 0;
}

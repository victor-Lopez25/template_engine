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

#if defined(_WIN32) && defined(SHORTCUT_PATH)
# define SHORTCUT_PATH_ARG , "-DSHORTCUT_PATH=\"" SHORTCUT_PATH "\""
#else
# define SHORTCUT_PATH_ARG
#endif

#define VL_REBUILD_URSELF(bin, src) VL_DEFAULT_REBUILD_URSELF(bin, src), SHORTCUT_PATH_ARG
#define VL_BUILD_IMPLEMENTATION
#include "vl_build.h"

#if defined(_WIN32)
#include <consoleapi.h>
#endif

char *selfPath;

void Usage(char *program)
{
    printf("Usage: %s <template name>\n"
           "Possible templates are:\n"
           " - SDL3\n"
           " - SDL3-hotreload\n"
           " - SDL3-gpu\n"
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

void GetInfo(Template t)
{
    fprintf(stderr, "[INFO] '%s':\n", TemplateToString(t));
    string_builder sb = {0};
    char *readmeFileFmt = 0;
    
    switch(t) {
        case Template_SDL3: {
            readmeFileFmt = "%s/template_files/SDL3/README.md";
        } break;
        case Template_SDL3_Hotreload: {
            readmeFileFmt = "%s/template_files/SDL3/README_hotreload.md";
        } break;
        case Template_SDL3_GPU_Hotreload: {
            readmeFileFmt = "%s/template_files/SDL3/README_gpu.md";
        } break;
        
        case Template_None: case Count_Templates: Assert(!"unreachable");
    }

    if(!readmeFileFmt) {
        fprintf(stderr, "Missing info for this template, make an issue on github: https://github.com/victor-Lopez25/template_engine/issues\n");
        return;
    }
    sb_ReadEntireFile(temp_sprintf("%s/template_files/SDL3/README.md", selfPath), &sb);
    fprintf(stderr, "%.*s\n\n", (int)sb.count, sb.items);
}

#define SetupGeneralSDL3Templates(...) \
    SetupGeneralSDL3Templates_(((const char*[]){"SDL3", __VA_ARGS__}), \
                               (sizeof((const char*[]){"SDL3", __VA_ARGS__})/sizeof(const char*)))
void SetupGeneralSDL3Templates_(const char **libs, size_t libcount)
{
    MkdirIfNotExist("src");
    MkdirIfNotExist("lib");
    MkdirIfNotExist("include");
    MkdirIfNotExist("dependencies");

    size_t mark = temp_save();

    for(size_t i = 0; i < libcount; i++) {
        VL_CopyFile(temp_sprintf("%s/template_files/SDL3/bin/%s.dll", selfPath, libs[i]), temp_sprintf("dependencies/%s.dll", libs[i]));
        VL_CopyFile(temp_sprintf("%s/template_files/SDL3/lib/%s.lib", selfPath, libs[i]), temp_sprintf("lib/%s.lib", libs[i]));
    }

    VL_CopyDirectoryRecursively(temp_sprintf("%s/template_files/SDL3/include", selfPath), "include/SDL3");

    VL_CopyFile(temp_sprintf("%s/template_files/spall.h", selfPath), "src/spall.h");
    VL_CopyFile(temp_sprintf("%s/viclib.h", selfPath), "src/viclib.h");
    VL_CopyFile(temp_sprintf("%s/vl_build.h", selfPath), "vl_build.h");

    temp_rewind(mark);
}

void DoTemplate(Template chosen)
{
    Assert(chosen > Template_None || chosen < Count_Templates);

    VL_Log(VL_INFO, "Chosen template: %s\n", TemplateToString(chosen));
    switch(chosen) {
        case Template_SDL3: {
            // SDL3_mixer also?
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf");

            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/main.c", selfPath), "src/main.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/build.c", selfPath), "build.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/README.md", selfPath), "README.md");
        } break;

        case Template_SDL3_Hotreload: {
            // SDL3_mixer also?
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf");

            VL_CopyFile(temp_sprintf("%s/template_files/main_hot_reload.c", selfPath), "src/main_hot_reload.c");
            VL_CopyFile(temp_sprintf("%s/template_files/main_no_hot_reload.c", selfPath), "src/main_no_hot_reload.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/app.c", selfPath), "src/app.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/build_hotreload.c", selfPath), "build.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/README_hotreload.md", selfPath), "README.md");
        } break;

        case Template_SDL3_GPU_Hotreload: {
            SetupGeneralSDL3Templates("SDL3_image", "SDL3_ttf", "SDL3_shadercross");

            VL_CopyDirectoryRecursively(temp_sprintf("%s/template_files/SDL3/shaders", selfPath), "shaders");

            VL_CopyFile(temp_sprintf("%s/template_files/main_hot_reload.c", selfPath), "src/main_hot_reload.c");
            VL_CopyFile(temp_sprintf("%s/template_files/main_no_hot_reload.c", selfPath), "src/main_no_hot_reload.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/app_gpu.c", selfPath), "src/app.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/build_gpu.c", selfPath), "build.c");
            VL_CopyFile(temp_sprintf("%s/template_files/SDL3/README_gpu.md", selfPath), "README.md");

            MkdirIfNotExist("resources");
            VL_CopyFile(temp_sprintf("%s/template_files/resources/cat.jpg", selfPath), "resources/cat.jpg");
        } break;

        case Template_None: case Count_Templates: break;
    }
}

bool TestTemplate(vl_cmd *cmd, Template chosen)
{
    Assert(chosen > Template_None || chosen < Count_Templates);

#define X(...) { \
    .items = (const char*[]){__VA_ARGS__}, \
    .count = (sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*)) }
        struct { const char **items; size_t count; } cmdItems[] = {
#if defined(_WIN32)
            X("gcc", "build.c", "-o", "build.exe", "-Wall", "-Wextra", "-Werror"),
            X("clang", "build.c", "-o", "build.exe", "-Wall", "-Wextra", "-Werror"),
            X("cl", "build.c", "/nologo", "-FC", "-GR-", "-EHa", "-W4", "-WX", "-D_CRT_SECURE_NO_WARNINGS"),
#else
            X("cc", "build.c", "-o", "build", "-Wall", "-Wextra", "-Werror"),
#endif
        };
#undef X

    switch(chosen) {
        case Template_SDL3: {
            for(size_t i = 0; i < ArrayLen(cmdItems); i++) {
                da_AppendMany(cmd, cmdItems[i].items, cmdItems[i].count);
                if(!CmdRun(cmd)) return false;
                cmd_Append(cmd, "build", "test");
                if(!CmdRun(cmd)) return false;
            }
        } break;

        case Template_SDL3_Hotreload:
        case Template_SDL3_GPU_Hotreload: {
            for(size_t i = 0; i < ArrayLen(cmdItems); i++) {
                da_AppendMany(cmd, cmdItems[i].items, cmdItems[i].count);
                if(!CmdRun(cmd)) return false;
                cmd_Append(cmd, "build", "test", "hotreload");
                if(!CmdRun(cmd)) return false;
                cmd_Append(cmd, "build", "test", "nohotreload");
                if(!CmdRun(cmd)) return false;
            }
        } break;

        case Template_None: case Count_Templates: return false;
    }

    return true;
}

bool RemoveDirectoryRecursive(const char *dir)
{
    file_type type = VL_GetFileType(dir);
    if(type == VL_FILE_DIRECTORY) { // directory
        vl_file_paths paths = {0};
        size_t mark = temp_save();
        VL_ReadEntireDir(dir, &paths);
        for(size_t pathIdx = 0; pathIdx < paths.count; pathIdx++)
        {
            if(strcmp(paths.items[pathIdx], ".") && strcmp(paths.items[pathIdx], "..")) {
                if(!RemoveDirectoryRecursive(temp_sprintf("%s/%s", dir, paths.items[pathIdx]))) {
                    VL_FREE(paths.items);
                    temp_rewind(mark);
                    return false;
                }
            }
        }
        VL_FREE(paths.items);
        temp_rewind(mark);
#if defined(_WIN32)
        return RemoveDirectoryA(dir);
#else
        return rmdir(dir) == 0;
#endif
    } else if(type == VL_FILE_REGULAR) { // normal file
        return VL_DeleteFile(dir);
    } else if(type == VL_FILE_SYMLINK) {
#if defined(_WIN32)
        VL_Log(VL_WARNING, "Symlink deleting is not supported on windows for now, skipped deleting '%s'", dir);
        return true;
#else
        return unlink(dir) == 0;
#endif
    } else if(type == VL_FILE_OTHER) {
        VL_Log(VL_WARNING, "Unknown file type of file '%s'", dir);
        return true;
    }
    return false;
}

bool ConsoleSupportsColor(void)
{
    static bool supports = false;
    static bool tested = false;
    if(tested) return supports;

#if defined(_WIN32)
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if(GetConsoleMode(out, &mode)) {
        supports = mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    }
#else
    supports = system("tput colors > /dev/null 2>&1") == 0;
#endif
    tested = true;
    return supports;
}

const char *SuccessOrFail(bool success)
{
    if(ConsoleSupportsColor()) {
        return success ? ("\033[0;32m" "success" "\033[0m") : 
                         ("\033[0;31m" "fail"    "\033[0m");
    }
    return success ? "success" : "fail";
}

void Test(void)
{
    vl_cmd cmd = {0};
    bool returnOnFirstFail = true;
    bool success = true;
    VL_MinimalLogLevel = VL_ERROR;
    RemoveDirectoryRecursive("temp");
    for(int templateIdx = Template_None+1; templateIdx < Count_Templates; templateIdx++)
    {
        MkdirIfNotExist("temp");
        VL_SetCurrentDir("temp");

        DoTemplate((Template)templateIdx);
        VL_MinimalLogLevel = VL_INFO;

        bool ok = TestTemplate(&cmd, (Template)templateIdx);
        VL_MinimalLogLevel = VL_ERROR;

        printf("Test: %s - %s\n", TemplateToString((Template)templateIdx), SuccessOrFail(ok));
        VL_SetCurrentDir("..");
        if(!ok) {
            if(returnOnFirstFail) return;
            else success = false;
        }
        if(!RemoveDirectoryRecursive("temp")) return;
    }
    printf("\nTests %s\n", SuccessOrFail(success));
}

int main(int argc, char **argv)
{
    char *exe_path = VL_temp_RunningExecutablePath();
    selfPath = VL_temp_DirName(exe_path);
    for(int i = (int)strlen(selfPath) - 1; i >= 0; i--) {
        if(selfPath[i] == '/' || selfPath[i] == '\\') {
            selfPath[i] = '\0';
            break;
        }
    }

    const char *current_dir = VL_temp_GetCurrentDir();
    bool inExeDirectory = !strcmp(selfPath, current_dir);

    bool gettingInfo = false;
    bool gotInfo = false;
    for(int argIdx = 1; argIdx < argc; argIdx++) {
        char *arg = argv[argIdx];
        if(!strcmp(arg, "-h") || !strcmp(arg, "--help") || !strcmp(arg, "/?")) {
            Usage(*argv);
            return 0;
        } else if(!strcmp(arg, "SDL3")) {
            chosenTemplate = Template_SDL3;
            if(gettingInfo) { GetInfo(chosenTemplate); gotInfo = true; }
        } else if(!strcmp(arg, "SDL3-hotreload")) {
            chosenTemplate = Template_SDL3_Hotreload;
            if(gettingInfo) { GetInfo(chosenTemplate); gotInfo = true; }
        } else if(!strcmp(arg, "SDL3-gpu")) {
            chosenTemplate = Template_SDL3_GPU_Hotreload;
            if(gettingInfo) { GetInfo(chosenTemplate); gotInfo = true; }
        } else if(!strcmp(arg, "test")) {
            Test();
            if(gettingInfo) {
                fprintf(stderr, "[INFO] 'test': The 'test' flag is used mostly internally to check if each template passes its tests\n");
                gotInfo = true;
            }
            return 0;
        } else if(!strcmp(arg, "info")) {
            gettingInfo = true;
        }
    }

    if(inExeDirectory) {
        VL_GO_REBUILD_URSELF(argc, argv);
#if defined(_WIN32) && defined(SHORTCUT_PATH)
        size_t exe_name_len = strlen(argv[0]);
        if(exe_name_len < 5 || strcmp(argv[0] + exe_name_len - 4, ".exe")) {
            VL_Log(NOB_ERROR, "Need executable to have '.exe' extension to make a shortcut");
        } else {
            system(temp_sprintf("make_shortcut.bat %.*s "SHORTCUT_PATH, exe_name_len - 4, argv[0]));
            VL_Log(NOB_INFO, "Created shortcut in directory: "SHORTCUT_PATH);
        }
#endif
    }

    if(gettingInfo) {
        if(gotInfo) return 0;
        else {
            Usage(*argv);
            fprintf(stderr, "Get info of a specific template doing %s info <template>\n", *argv);
            return 1;
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

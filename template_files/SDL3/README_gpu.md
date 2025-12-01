## SDL3 gpu template (work in progress)

All you need to build this template is run a compiler on build.c:

### Step 1
If on linux/mac:
```bash
cc build.c -o build
```

If on windows:

GCC: `gcc build.c -o build.exe`

clang: `clang build.c -o build.exe`

msvc: `cl build.c -nologo`

### Step 2
Then run `build(.exe)` in the same folder and it'll recompile itself if any changes were made to it. It'll also compile the template and run it if "norun" isn't specified.

If for any reason you want to not hot reload, you can run `build(.exe) nohotreload` and it'll #include app.c instead of linking dynamically to it. Why? You might want this for release builds. This does not include shaders, they will still be hot reloaded.

## Dependencies

On windows, vendored.

On linux/mac, compile from source or try to find packages 'sdl3', 'sdl3-image', 'sdl3-ttf' or similar (need the 3 unless the template is changed)
You will have to probably compile SDL3_shadercross from source since it's not included yet in many package managers: https://github.com/libsdl-org/SDL_shadercross

WARNING: If trying hot reloading of shaders specifically on vscode with auto saving, keep in mind vscode sometimes doesn't save for small changes so actually save too (thought this was an issue in my program lol)

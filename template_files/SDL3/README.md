## SDL3 template

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

## Dependencies

On windows, vendored.

On linux/mac, compile from source or try to find packages 'sdl3', 'sdl3-image', 'sdl3-ttf' or similar (need the 3 unless the template is changed)

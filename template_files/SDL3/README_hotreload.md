### SDL3 hot reload template

All you need to build this template is run a compiler on nob.c:

If on linux/mac:
```bash
cc nob.c -o nob
```

If on windows:

GCC: `gcc nob.c -o nob.exe`

clang: `clang nob.c -o nob.exe`

msvc: `cl nob.c -nologo`

Then run `nob(.exe)` in the same folder and it'll recompile itself if any changes were made to it. It'll also compile the template and run it if "norun" isn't specified.

If for any reason you want to not hot reload, you can run `nob(.exe) nohotreload` and it'll #include app.c instead of linking dynamically to it. Why? You might want this for release builds.

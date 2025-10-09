## template engine for projects

This is intended as a compilation of templates I use to remove the need to do every time a lot of 'glue' when starting a project (eg. linking libraries, creating scripts for compilation, etc.)

Included templates:
 - SDL3: SDL_image, SDL_ttf setup + A thread pool made by yours truely (doesn't support adding work from multiple threads)
 - SDL3-hotreload: SDL3 template + hot reloading
 - SDL3-gpu: SDL3-hotreload template + rotating texture on gpu + hot reloaded shaders with reflection!

Even though there is a template called SDL3-gpu, this does not mean the others don't use the gpu, SDL3-gpu uses the SDL3_gpu api and the other SDL3 templates use the SDL3_renderer api, that's the difference.

I will look into issues but in general, I won't take pull requests unless they are for minor changes.

### Usage

Once compiled, you may run template_engine from a different directory like follows:

```bash
# example directory
mkdir temp
cd temp
../template_engine <template name>
```

### Building

Mac untested!!

All you need to build the template engine is run a compiler on main.c:

If on linux/mac:
```bash
cc main.c -o template_engine
```

If on windows:

GCC: `gcc main.c -o template_engine.exe`

clang: `clang main.c -o template_engine.exe`

msvc: `cl main.c -Fe:template_engine.exe -nologo`

Then, if you do any changes, just run `template_engine(.exe)` in the same folder and it will recompile itself

The templates work exactly the same, just replace main.c with nob.c and the output should be nob(.exe), then run nob(.exe) and it'll compile the template and run it. (PS: msvc doesn't need the name of the executable if it's the same as the source)

### Licencing

Any file with a name starting with SDL or SDL_ is licenced with SDL's Zlib license. See: https://github.com/libsdl-org/SDL?tab=Zlib-1-ov-file

Files used as a template for projects are under the unlicense license unless stated otherwise, for example spall.h is licensed under MIT by Phillip Trudeau-Tavara (as stated in spall.h)

Files that compile to the program that provides these are licensed under MIT license.


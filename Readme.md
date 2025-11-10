## template engine for projects

This is intended as a compilation of templates I use to remove the need to do every time a lot of 'glue' when starting a project (eg. linking libraries, creating scripts for compilation, etc.)

Included templates:
 - SDL3: SDL_image, SDL_ttf setup + A thread pool made by yours truely (doesn't support adding work from multiple threads)
 - SDL3-hotreload: SDL3 template + hot reloading
 - SDL3-gpu: SDL3-hotreload template + rotating texture on gpu + hot reloaded shaders with reflection!

Even though there is a template called SDL3-gpu, this does not mean the others don't use the gpu, SDL3-gpu uses the SDL3_gpu api and the other SDL3 templates use the SDL3_renderer api, that's the difference.

I will look into issues but in general, I won't take pull requests unless they are for minor changes.

### Quick start

```bash
# This first command to compile the template engine.
# If you want to compile it again after changes, just run template_engine in the same directory.
cc main.c -o template_engine

# these commands to use a template from template_engine
mkdir template
cd template
../template_engine SDL-hotreload
# after compiling nob.c once, you can run nob and it'll compile itself if needed
cc nob.c -o nob
nob
```
if on windows, add '.exe' to executables and use any c compiler, 'gcc', 'clang', or 'cl' instead of 'cc'

Some info specific of each template can be found in their READMEs after copying them or using the 'info' command

### Licencing

Any file with a name starting with SDL or SDL_ is licenced with SDL's Zlib license. See: https://github.com/libsdl-org/SDL?tab=Zlib-1-ov-file

Files used as a template for projects are under the unlicense license unless stated otherwise, for example spall.h is licensed under MIT by Phillip Trudeau-Tavara (as stated in spall.h)

Files that compile to the program that provides these are licensed under MIT license.


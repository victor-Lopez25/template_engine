#include <SDL3/SDL_ttf.h>
#include <SDL3/SDL_image.h>

#include "sdl_common.h"
#include "sdl_common.c"

#include "spall.h"

#ifndef DLL_EXPORT
# define DLL_EXPORT __declspec(dllexport)
#endif

typedef struct {
    ProgramInput input;
    SDL_Window *window;
    SDL_Renderer *renderer;
    Sint32 windowWidth;
    Sint32 windowHeight;
    float deltaTime;
    float targetFPS;

    WorkQueue workQueue;

#define SPALL_BUFFER_SIZE 1024*1024
    SpallBuffer spall_buffer;
    SpallProfile spall_ctx;

    SDL_AtomicInt targetFPSMissedCause;
} ProgramContext;

DLL_EXPORT size_t MemorySize(void) { return sizeof(ProgramContext); }

void AppRender(ProgramContext *ctx);

bool FilterSDL3Events(void *userdata, SDL_Event *event)
{
    ProgramContext *ctx = (ProgramContext*)userdata;
    switch(event->type) {
        // Resized to event->window. data1xdata2
        case SDL_EVENT_WINDOW_RESIZED: {
            ctx->windowWidth = event->window.data1;
            ctx->windowHeight = event->window.data2;
            AppRender(ctx);
            SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_WindowResize);
            return false;
        }

        // Moved to event->window. data1xdata2
        case SDL_EVENT_WINDOW_MOVED: {
            AppRender(ctx);
            SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_WindowMove);
            return false;
        }

        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_WindowFocus);
        } break;
    }

    return true;
}

//////////////////////////////////////////////////////////

DLL_EXPORT bool AppInitPartial(void *rawdata)
{
    (void)rawdata;
    return true;
}

DLL_EXPORT bool AppInit(void *rawdata)
{
    ProgramContext *ctx = (ProgramContext*)rawdata;

    if(!spall_init_file("trace.spall", 1, &ctx->spall_ctx)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to setup spall");
        return false;
    }

    ctx->spall_buffer.data = SDL_malloc(SPALL_BUFFER_SIZE);
    if(!ctx->spall_buffer.data) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not allocate spall buffer");
        return false;
    }
    ctx->spall_buffer.length = SPALL_BUFFER_SIZE;
    if(!spall_buffer_init(&ctx->spall_ctx, &ctx->spall_buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init spall buffer");
        return false;
    }

    Spall_BufferBegin(&ctx->spall_ctx, &ctx->spall_buffer, __FUNCTION__);

    if(!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init sdl: %s", SDL_GetError());
        return false;
    }

    if(!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init sdl_ttf: %s", SDL_GetError());
        return false;
    }

    if(!SDL_CreateWindowAndRenderer("template", 800, 600, SDL_WINDOW_RESIZABLE, &ctx->window, &ctx->renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window and renderer: %s", SDL_GetError());
        return false;
    }

    SDL_GetWindowSize(ctx->window, &ctx->windowWidth, &ctx->windowHeight);
    SDL_SetEventFilter(FilterSDL3Events, ctx);

    SDL_memset4(ctx->input.regularKeyUp, 0x01010101, sizeof(ctx->input.regularKeyUp)/4);
    SDL_memset4(ctx->input.commandKeyUp, 0x01010101, sizeof(ctx->input.commandKeyUp)/4);
    SDL_memset4(ctx->input.extendedKeyUp, 0x01010101, sizeof(ctx->input.extendedKeyUp)/4);
    ctx->input.mouseLeft.up = true; ctx->input.mouseMiddle.up = true; ctx->input.mouseRight.up = true;
    ctx->input.mouseX1.up = true; ctx->input.mouseX2.up = true;

    ctx->deltaTime = 0.0f;
    ctx->targetFPS = 60.0f;

    if(!InitWorkQueue(&ctx->spall_ctx, &ctx->workQueue, 2)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init sdl work queue");
        return false;
    }

    bool ok = AppInitPartial(rawdata);

    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    return ok;
}

DLL_EXPORT void AppDeInitPartial(void *rawdata)
{
    (void)rawdata;
}

DLL_EXPORT void AppDeInit(void *rawdata)
{
    AppDeInitPartial(rawdata);

    ProgramContext *ctx = (ProgramContext*)rawdata;

    spall_buffer_quit(&ctx->spall_ctx, &ctx->spall_buffer);
    for(Uint32 threadIdx = 0; threadIdx < ctx->workQueue.threadCount; threadIdx++) {
        SDL_DetachThread(ctx->workQueue.threads[threadIdx]);
        spall_buffer_quit(&ctx->spall_ctx, &ctx->workQueue.spall_buffers[threadIdx]);
    }
    spall_quit(&ctx->spall_ctx);
    SDL_free(ctx->spall_buffer.data);
    SDL_free(ctx->workQueue.spall_buffers[0].data);
    SDL_free(ctx->workQueue.spall_buffers);

    TTF_Quit();

    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

void AppRender(ProgramContext *ctx)
{
    Spall_BufferBegin(&ctx->spall_ctx, &ctx->spall_buffer, __FUNCTION__);

    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    // Send render commands here.
    SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
    SDL_RenderDebugText(ctx->renderer, 100, 60, "debug text");

    SDL_RenderPresent(ctx->renderer);

    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);
}

void GetMouseInput(ProgramContext *ctx)
{
    SDL_MouseButtonFlags mouseFlags = SDL_GetMouseState(&ctx->input.mouseX, &ctx->input.mouseY);
#define X(button, sdl_mask)                     \
    if(mouseFlags & sdl_mask) {                 \
        if(button.up) button.pressed = true;    \
        button.down = true; button.up = false;  \
    } else {                                    \
        if(button.down) button.released = true; \
        button.up = true; button.down = false;  \
    }

    X(ctx->input.mouseLeft, SDL_BUTTON_LMASK)
    X(ctx->input.mouseMiddle, SDL_BUTTON_MMASK)
    X(ctx->input.mouseRight, SDL_BUTTON_RMASK)
    X(ctx->input.mouseX1, SDL_BUTTON_X1MASK)
    X(ctx->input.mouseX2, SDL_BUTTON_X2MASK)
#undef X
}

void ClearInput(ProgramContext *ctx)
{
    // Clear pressed and released keys
    SDL_memset4(ctx->input.regularKeyPressed, 0, sizeof(ctx->input.regularKeyPressed)/2);
    SDL_memset4(ctx->input.commandKeyPressed, 0, sizeof(ctx->input.commandKeyPressed)/2);
    SDL_memset4(ctx->input.extendedKeyPressed, 0, sizeof(ctx->input.extendedKeyPressed)/2);
#define ClearMouseButton(button) \
    button.pressed = false; button.released = false;
    ClearMouseButton(ctx->input.mouseLeft)
    ClearMouseButton(ctx->input.mouseMiddle)
    ClearMouseButton(ctx->input.mouseRight)
    ClearMouseButton(ctx->input.mouseX1)
    ClearMouseButton(ctx->input.mouseX2)
#undef ClearMouseButton
    ctx->input.mouseWheelX = 0.0f;
    ctx->input.mouseWheelY = 0.0f;
}

DLL_EXPORT bool MainLoop(void *rawdata)
{
    ProgramContext *ctx = (ProgramContext*)rawdata;

    bool quit = false;
    Uint64 startTick = SDL_GetTicksNS();
    spall_buffer_begin(&ctx->spall_ctx, &ctx->spall_buffer, "Update and Render", (int32_t)strlen("Update and Render"), startTick);
    spall_buffer_begin(&ctx->spall_ctx, &ctx->spall_buffer, "Fetch Input", (int32_t)strlen("Fetch Input"), startTick);

    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_EVENT_QUIT: {
                quit = true;
                SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_End);
            } break;

            case SDL_EVENT_KEY_DOWN: {
                HandleSDLKeyDownEvent(&ctx->input, &event);
            } break;

            case SDL_EVENT_KEY_UP: {
                HandleSDLKeyUpEvent(&ctx->input, &event);
            } break;

            case SDL_EVENT_MOUSE_WHEEL: {
                ctx->input.mouseWheelX += event.wheel.x;
                ctx->input.mouseWheelY += event.wheel.y;
            } break;
        }
    }

    GetMouseInput(ctx);
    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    AppRender(ctx);

    Uint64 targetTicks = (Uint64)(1000000000.0f/ctx->targetFPS);
    Uint64 endTick = SDL_GetTicksNS();
    spall_buffer_end(&ctx->spall_ctx, &ctx->spall_buffer, endTick);
    Uint64 difTicks = endTick - startTick;
    if(difTicks < targetTicks) {
        SDL_DelayPrecise(targetTicks - difTicks);
        difTicks = SDL_GetTicksNS() - startTick;
    } else {
        int cause = SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_Regular);
        if(cause < TargetFPSMissed_Irrelevant) {
            SDL_Log("Missed target fps: %fms - %s", (float)difTicks/1000000.0f, GetTargetFPSCauseString(cause));
        }
    }
    ctx->deltaTime = (float)difTicks/1000000000.0f;

    return quit;
}

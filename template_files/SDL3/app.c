#include <SDL3/SDL.h>
#include <SDL3/SDL_ttf.h>
#include <SDL3/SDL_image.h>

#include "spall.h"

#ifndef DLL_EXPORT
# define DLL_EXPORT __declspec(dllexport)
#endif

#define Spall_BufferBegin(ctx, buf, name) spall_buffer_begin(ctx, buf, name, (int32_t)SDL_strlen(name), SDL_GetTicksNS())
#define Spall_BufferEnd(ctx, buf) spall_buffer_end(ctx, buf, SDL_GetTicksNS())

typedef enum {
    TargetFPSMissed_Regular, // No specific reason - must log since this shouldn't happen regularly
    //TargetFPSMissed_Init, // First frame
    TargetFPSMissed_End, // Last frame
    TargetFPSMissed_Irrelevant, // Anything below this doesn't need to be logged
    TargetFPSMissed_WindowResize,
    TargetFPSMissed_WindowMove,
    TargetFPSMissed_WindowFocus,
} TargetFPSMissedCauses;

const char *GetTargetFPSCauseString(int cause) {
    switch(cause) {
        case TargetFPSMissed_Regular: return "Performance";
        case TargetFPSMissed_End: return "Last frame before window closes";
        case TargetFPSMissed_Irrelevant: return "Irrelevant";
        case TargetFPSMissed_WindowFocus: return "Window Focus";
        case TargetFPSMissed_WindowResize: return "Window Resize";
        case TargetFPSMissed_WindowMove: return "Window Move";
        default: return "Unknown value";
    }
}

typedef int (*ThreadWorkCallback)(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, void *data);

typedef struct {
    ThreadWorkCallback callback;
    void *data;
} WorkQueueEntry;

typedef struct {
    SDL_AtomicInt completionGoal;
    SDL_AtomicInt completionCount;
    volatile Uint32 nextEntryToWrite;
    SDL_AtomicInt nextEntryToRead;
    SDL_Semaphore *semaphore;

    WorkQueueEntry entries[256];

    Uint32 threadCount;
    SDL_Thread **threads;

    SpallProfile *spall_ctx;
    SpallBuffer *spall_buffers;
} WorkQueue;

typedef struct {
  bool down;
  bool up;
  bool pressed;
  bool released;
} InputButton;

typedef struct {
  bool regularKeysDown[0xFFF];
  bool regularKeysUp[0xFFF];
  bool regularKeysPressed[0xFFF];
  bool regularKeysReleased[0xFFF];
  // any other keys with SDLK_xyz > 0xFFF

  InputButton mouseLeft;
  InputButton mouseMiddle;
  InputButton mouseRight;
  InputButton mouseX1;
  InputButton mouseX2;
  float mouseX, mouseY;
  float mouseWheelX, mouseWheelY;
} ProgramInput;

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

void RenderAll(ProgramContext *ctx);

bool FilterSDL3Events(void *userdata, SDL_Event *event)
{
    ProgramContext *ctx = (ProgramContext*)userdata;
    switch(event->type) {
        // Resized to event->window. data1xdata2
        case SDL_EVENT_WINDOW_RESIZED: {
            ctx->windowWidth = event->window.data1;
            ctx->windowHeight = event->window.data2;
            RenderAll(ctx);
            SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_WindowResize);
            return false;
        }

        // Moved to event->window. data1xdata2
        case SDL_EVENT_WINDOW_MOVED: {
            RenderAll(ctx);
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
// work queue stuff

void AddWorkEntry(WorkQueue *queue, ThreadWorkCallback callback, void *data)
{
    Uint32 nextEntryToWrite = (queue->nextEntryToWrite + 1) % SDL_arraysize(queue->entries);
    // TODO: If this is the case, the work queue should be larger/resizable
    SDL_assert((int)nextEntryToWrite != SDL_GetAtomicInt(&queue->nextEntryToRead));
    WorkQueueEntry *entry = &queue->entries[queue->nextEntryToWrite];
    entry->callback = callback;
    entry->data = data;
    SDL_AddAtomicInt(&queue->completionGoal, 1);
    SDL_CompilerBarrier();
    queue->nextEntryToWrite = nextEntryToWrite;
    SDL_SignalSemaphore(queue->semaphore);
}

bool DoNextWorkEntry(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue)
{
    bool shouldSleep = false;

    Uint32 originalNextEntryToRead = SDL_GetAtomicInt(&queue->nextEntryToRead);
    Uint32 nextEntryToRead = (originalNextEntryToRead + 1) % SDL_arraysize(queue->entries);
    if(originalNextEntryToRead != queue->nextEntryToWrite) {
        if(SDL_CompareAndSwapAtomicInt(&queue->nextEntryToRead, originalNextEntryToRead, nextEntryToRead)) {
            WorkQueueEntry *entry = &queue->entries[originalNextEntryToRead];
            entry->callback(spall_ctx, spall_buffer, entry->data);
            SDL_AddAtomicInt(&queue->completionCount, 1);
        }
    } else {
        shouldSleep = true;
    }

    return shouldSleep;
}

void CompleteAllWorkerEntries(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue)
{
    while(SDL_GetAtomicInt(&queue->completionGoal) != SDL_GetAtomicInt(&queue->completionCount)) {
        DoNextWorkEntry(spall_ctx, spall_buffer, queue);
    }

    SDL_SetAtomicInt(&queue->completionGoal, 0);
    SDL_SetAtomicInt(&queue->completionCount, 0);
}

typedef struct {
    WorkQueue *queue;
    int threadIdx;
} ThreadData;

int SDLCALL ThreadProc(void *data)
{
    ThreadData *tdata = (ThreadData*)data;
    WorkQueue *queue = tdata->queue;
    int threadIdx = tdata->threadIdx;
    SDL_free(data);
    
    for(;;) {
        if(DoNextWorkEntry(queue->spall_ctx, &queue->spall_buffers[threadIdx], queue)) {
            SDL_WaitSemaphore(queue->semaphore);
        }
    }

    // NOTE: This doesn't return
    //return 0;
}

bool InitWorkQueue(SpallProfile *spall_ctx, WorkQueue *queue, Uint32 threadCount)
{
    SDL_SetAtomicInt(&queue->completionGoal, 0);
    SDL_SetAtomicInt(&queue->completionCount, 0);

    queue->nextEntryToWrite = 0;
    SDL_SetAtomicInt(&queue->nextEntryToRead, 0);

    // TODO: Error reporting
    queue->semaphore = SDL_CreateSemaphore(0);
    if(!queue->semaphore) return false;

    queue->threads = SDL_malloc(threadCount*sizeof(SDL_Thread*));
    if(!queue->threads) return false;

    queue->spall_ctx = spall_ctx;
    queue->spall_buffers = SDL_malloc(threadCount*sizeof(SpallBuffer));
    if(!queue->spall_buffers) return false;

    Uint8 *backingBuffer = SDL_malloc(threadCount*SPALL_BUFFER_SIZE);
    if(!backingBuffer) return false;

    queue->threadCount = threadCount;
    char threadNameBuf[40];
    for(Uint32 threadIdx = 0; threadIdx < threadCount; threadIdx++) {
        ThreadData *tdata = SDL_malloc(sizeof(ThreadData));
        if(!tdata) return false;
        tdata->queue = queue;
        tdata->threadIdx = threadIdx;

        queue->spall_buffers[threadIdx].data = backingBuffer + threadIdx*SPALL_BUFFER_SIZE;
        queue->spall_buffers[threadIdx].length = SPALL_BUFFER_SIZE;
        queue->spall_buffers[threadIdx].tid = threadIdx + 1; // (uint32_t)SDL_GetThreadID(queue->threads[threadIdx]);
        if(!spall_buffer_init(spall_ctx, &queue->spall_buffers[threadIdx])) return false;

        SDL_snprintf(threadNameBuf, sizeof(threadNameBuf), "Worker thread %u", threadIdx);
        queue->threads[threadIdx] = SDL_CreateThread(ThreadProc, threadNameBuf, tdata);
        if(!queue->threads[threadIdx]) return false;
    }

    return true;
}

//////////////////////////////////////////////////////////

DLL_EXPORT void InitPartial(void *rawdata)
{
    (void) rawdata;
}

DLL_EXPORT bool InitAll(void *rawdata)
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

    SDL_memset4(ctx->input.regularKeysUp, 0x01010101, sizeof(ctx->input.regularKeysUp)/4);
    ctx->input.mouseLeft.up = true; ctx->input.mouseMiddle.up = true; ctx->input.mouseRight.up = true;
    ctx->input.mouseX1.up = true; ctx->input.mouseX2.up = true;

    ctx->deltaTime = 0.0f;
    ctx->targetFPS = 60.0f;

    bool ok = InitWorkQueue(&ctx->spall_ctx, &ctx->workQueue, 2);

    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    InitPartial(rawdata);

    return ok;
}

DLL_EXPORT void DeInitPartial(void *rawdata)
{
    (void)rawdata;
}

DLL_EXPORT void DeInitAll(void *rawdata)
{
    DeInitPartial(rawdata);

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

void RenderAll(ProgramContext *ctx)
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
    SDL_memset4(ctx->input.regularKeysPressed, 0, sizeof(ctx->input.regularKeysPressed)/2);
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
                if(event.key.key < 0xFFF) {
                    if(ctx->input.regularKeysUp[event.key.key]) ctx->input.regularKeysPressed[event.key.key] = true;
                    ctx->input.regularKeysDown[event.key.key] = true;
                    ctx->input.regularKeysUp[event.key.key] = false;
                }
                //SDL_Log("key down: %u", event.key.key);
            } break;

            case SDL_EVENT_KEY_UP: {
                if(event.key.key < 0xFFF) {
                    if(ctx->input.regularKeysDown[event.key.key]) ctx->input.regularKeysReleased[event.key.key] = true;
                    ctx->input.regularKeysUp[event.key.key] = true;
                    ctx->input.regularKeysDown[event.key.key] = false;
                }
            } break;

            case SDL_EVENT_MOUSE_WHEEL: {
                ctx->input.mouseWheelX += event.wheel.x;
                ctx->input.mouseWheelY += event.wheel.y;
            } break;
        }
    }

    GetMouseInput(ctx);
    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    RenderAll(ctx);

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

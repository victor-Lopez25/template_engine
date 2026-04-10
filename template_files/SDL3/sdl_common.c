#include "sdl_common.h"

const char *GetTargetFPSCauseString(int cause)
{
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

static bool DoNextWorkEntry(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue)
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

static int SDLCALL ThreadProc(void *data)
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
#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#endif
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

void HandleSDLKeyDownEvent(ProgramInput *input, SDL_Event *event)
{
    if(event->key.key <= SDL_REGULAR_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_REGULAR_KEYS_START;
        if(input->regularKeyUp[key]) input->regularKeyPressed[key] = true;
        input->regularKeyDown[key] = true;
        input->regularKeyUp[key] = false;
    } else if(event->key.key >= SDL_COMMAND_KEYS_START && event->key.key <= SDL_COMMAND_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_COMMAND_KEYS_START;
        if(input->commandKeyUp[key]) input->commandKeyPressed[key] = true;
        input->commandKeyDown[key] = true;
        input->commandKeyUp[key] = false;
    } else if(event->key.key >= SDL_EXTENDED_KEYS_START && event->key.key <= SDL_EXTENDED_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_EXTENDED_KEYS_START;
        if(input->extendedKeyUp[key]) input->extendedKeyPressed[key] = true;
        input->extendedKeyDown[key] = true;
        input->extendedKeyUp[key] = false;
    }
}

void HandleSDLKeyUpEvent(ProgramInput *input, SDL_Event *event)
{
    if(event->key.key <= SDL_REGULAR_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_REGULAR_KEYS_START;
        if(input->regularKeyDown[key]) input->regularKeyReleased[key] = true;
        input->regularKeyUp[key] = true;
        input->regularKeyDown[key] = false;
    } else if(event->key.key >= SDL_COMMAND_KEYS_START && event->key.key <= SDL_COMMAND_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_COMMAND_KEYS_START;
        if(input->commandKeyDown[key]) input->commandKeyReleased[key] = true;
        input->commandKeyUp[key] = true;
        input->commandKeyDown[key] = false;
    } else if(event->key.key >= SDL_EXTENDED_KEYS_START && event->key.key <= SDL_EXTENDED_KEYS_END) {
        SDL_Keycode key = event->key.key - SDL_EXTENDED_KEYS_START;
        if(input->extendedKeyDown[key]) input->extendedKeyReleased[key] = true;
        input->extendedKeyUp[key] = true;
        input->extendedKeyDown[key] = false;
    }
}

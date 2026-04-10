#ifndef SDL_COMMON_H
#define SDL_COMMON_H

#include <SDL3/SDL.h>

#include "spall.h"

#define SPALL_BUFFER_SIZE 1024*1024

#define SDL_REGULAR_KEYS_START  SDLK_UNKNOWN
#define SDL_COMMAND_KEYS_START  SDLK_CAPSLOCK
#define SDL_EXTENDED_KEYS_START SDLK_LEFT_TAB

#define SDL_REGULAR_KEYS_END  SDLK_PLUSMINUS
#define SDL_COMMAND_KEYS_END  SDLK_CALL
#define SDL_EXTENDED_KEYS_END SDLK_RHYPER

#define SDL_REGULAR_KEYS_SIZE  (((SDL_REGULAR_KEYS_END - SDL_REGULAR_KEYS_START) + 3) & ~3)
#define SDL_COMMAND_KEYS_SIZE  (((SDL_COMMAND_KEYS_END - SDL_COMMAND_KEYS_START) + 3) & ~3)
#define SDL_EXTENDED_KEYS_SIZE (((SDL_EXTENDED_KEYS_END - SDL_EXTENDED_KEYS_START) + 3) & ~3)

#if SDL_REGULAR_KEYS_START != 0
#error This is not currently allowed, you must modify HandleSDLKey[Up|Down]Event and IsKeyDoingSomething
#endif

typedef enum {
    TargetFPSMissed_Regular, // No specific reason - must log since this shouldn't happen regularly
    //TargetFPSMissed_Init, // First frame
    TargetFPSMissed_End, // Last frame
    TargetFPSMissed_Irrelevant, // Anything below this doesn't need to be logged
    TargetFPSMissed_WindowResize,
    TargetFPSMissed_WindowMove,
    TargetFPSMissed_WindowFocus,
} TargetFPSMissedCauses;
const char *GetTargetFPSCauseString(int cause);

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
    bool regularKeyDown[SDL_REGULAR_KEYS_SIZE];
    bool regularKeyUp[SDL_REGULAR_KEYS_SIZE];
    bool regularKeyPressed[SDL_REGULAR_KEYS_SIZE];
    bool regularKeyReleased[SDL_REGULAR_KEYS_SIZE];

    bool commandKeyDown[SDL_COMMAND_KEYS_SIZE];
    bool commandKeyUp[SDL_COMMAND_KEYS_SIZE];
    bool commandKeyPressed[SDL_COMMAND_KEYS_SIZE];
    bool commandKeyReleased[SDL_COMMAND_KEYS_SIZE];

    bool extendedKeyDown[SDL_EXTENDED_KEYS_SIZE];
    bool extendedKeyUp[SDL_EXTENDED_KEYS_SIZE];
    bool extendedKeyPressed[SDL_EXTENDED_KEYS_SIZE];
    bool extendedKeyReleased[SDL_EXTENDED_KEYS_SIZE];

    InputButton mouseLeft;
    InputButton mouseMiddle;
    InputButton mouseRight;
    InputButton mouseX1;
    InputButton mouseX2;
    float mouseX, mouseY;
    float mouseWheelX, mouseWheelY;
} ProgramInput;

typedef struct {
    WorkQueue *queue;
    int threadIdx;
} ThreadData;

#define Spall_BufferBegin(ctx, buf, name) spall_buffer_begin(ctx, buf, name, (int32_t)SDL_strlen(name), SDL_GetTicksNS())
#define Spall_BufferEnd(ctx, buf) spall_buffer_end(ctx, buf, SDL_GetTicksNS())

#define IsKeyDoingSomething(input, what, keycode) \
  ((keycode >= SDL_REGULAR_KEYS_START && keycode <= SDL_REGULAR_KEYS_END) ? \
    ((input).regularKey##what[keycode]) : \
    (keycode >= SDL_COMMAND_KEYS_START && keycode <= SDL_COMMAND_KEYS_END) ? \
     ((input).commandKey##what[keycode - SDL_COMMAND_KEYS_START]) : \
     (keycode >= SDL_EXTENDED_KEYS_START && keycode <= SDL_EXTENDED_KEYS_END) ? \
      ((input).extendedKey##what[keycode - SDL_EXTENDED_KEYS_START]) : \
      false)

#define IsKeyDoingSomething_Ptr(input, what, keycode) \
  ((keycode >= SDL_REGULAR_KEYS_START && keycode <= SDL_REGULAR_KEYS_END) ? \
    ((input)->regularKey##what[keycode]) : \
    (keycode >= SDL_COMMAND_KEYS_START && keycode <= SDL_COMMAND_KEYS_END) ? \
     ((input)->commandKey##what[keycode] - SDL_COMMAND_KEYS_START) : \
     (keycode >= SDL_EXTENDED_KEYS_START && keycode <= SDL_EXTENDED_KEYS_END) ? \
      ((input)->extendedKey##what[keycode] - SDL_EXTENDED_KEYS_START) : \
      false)

#define IsKeyDown(input, keycode) IsKeyDoingSomething(input, Down, keycode)
#define IsKeyUp(input, keycode) IsKeyDoingSomething(input, Up, keycode)
#define IsKeyPressed(input, keycode) IsKeyDoingSomething(input, Pressed, keycode)
#define IsKeyReleased(input, keycode) IsKeyDoingSomething(input, Released, keycode)

#define IsKeyDown_Ptr(input, keycode) IsKeyDoingSomething_Ptr(input, Down, keycode)
#define IsKeyUp_Ptr(input, keycode) IsKeyDoingSomething_Ptr(input, Up, keycode)
#define IsKeyPressed_Ptr(input, keycode) IsKeyDoingSomething_Ptr(input, Pressed, keycode)
#define IsKeyReleased_Ptr(input, keycode) IsKeyDoingSomething_Ptr(input, Released, keycode)

void AddWorkEntry(WorkQueue *queue, ThreadWorkCallback callback, void *data);
void CompleteAllWorkerEntries(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue);
bool InitWorkQueue(SpallProfile *spall_ctx, WorkQueue *queue, Uint32 threadCount);

void HandleSDLKeyDownEvent(ProgramInput *input, SDL_Event *event);
void HandleSDLKeyUpEvent(ProgramInput *input, SDL_Event *event);

#endif // SDL_COMMON_H

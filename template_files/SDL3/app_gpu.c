#include <SDL3/SDL.h>
#include <SDL3/SDL_ttf.h>
#include <SDL3/SDL_image.h>
#include <SDL3/SDL_shadercross.h>

#include "spall.h"

#ifndef SHADER_DIRECTORY
#define SHADER_DIRECTORY "shaders/"
#endif

#if defined(_WIN32)
#define WINDOWS_LEAN_AND_MEAN
#include <windows.h>
bool GetLastWriteTime(const char *fileName, uint64_t *writeTime)
{
    bool ok = false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(GetFileAttributesEx(fileName, GetFileExInfoStandard, &data) &&
        (uint64_t)data.nFileSizeHigh + (uint64_t)data.nFileSizeLow > 0)
    {
        *writeTime = *(uint64_t*)&data.ftLastWriteTime;
        ok = true;
    }
    return ok;
}
#else
#include <sys/stat.h>
bool GetLastWriteTime(const char *fileName, uint64_t *writeTime)
{
    struct stat attr;
    bool ok = false;
    if(!stat(fileName, &attr) && attr.st_size > 0) {
        ok = true;
        *writeTime = *(uint64_t*)&attr.st_mtim;
    }
    return ok;
}
#endif

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
    SDL_AtomicU32 completionGoal;
    SDL_AtomicU32 completionCount;
    volatile Uint32 nextEntryToWrite;
    SDL_AtomicU32 nextEntryToRead;
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
    bool regularKeysDown[0xFF];
    bool regularKeysUp[0xFF];
    bool regularKeysPressed[0xFF];
    bool regularKeysReleased[0xFF];
    // any other keys with SDLK_xyz > 0xFF

    InputButton mouseLeft;
    InputButton mouseMiddle;
    InputButton mouseRight;
    InputButton mouseX1;
    InputButton mouseX2;
    float mouseX, mouseY;
    float mouseWheelX, mouseWheelY;
} ProgramInput;

typedef struct {
    SDL_GPUShader *shader;
    SDL_ShaderCross_GraphicsShaderMetadata *meta;
    Uint64 fileTime;
} ShaderInfo;

typedef struct {
    ProgramInput input;
    SDL_Window *window;
    SDL_GPUDevice *gpu;
    ShaderInfo vertexShader;
    ShaderInfo fragmentShader;

    SDL_GPUGraphicsPipeline *fillPipeline;
    SDL_GPUGraphicsPipeline *linePipeline;
    bool wireframeMode;

    SDL_GPUTextureFormat swapchainTextureFormat;
    Sint32 windowWidth;
    Sint32 windowHeight;
    float deltaTime;
    float targetFPS;

    SDL_GPUTextureFormat depthTextureFormat;
    SDL_GPUTexture *depthTexture;

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
    SDL_assert(nextEntryToWrite != SDL_GetAtomicU32(&queue->nextEntryToRead));
    WorkQueueEntry *entry = &queue->entries[queue->nextEntryToWrite];
    entry->callback = callback;
    entry->data = data;
    SDL_AddAtomicU32(&queue->completionGoal, 1);
    SDL_CompilerBarrier();
    queue->nextEntryToWrite = nextEntryToWrite;
    SDL_SignalSemaphore(queue->semaphore);
}

bool DoNextWorkEntry(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue)
{
    bool shouldSleep = false;

    Uint32 originalNextEntryToRead = SDL_GetAtomicU32(&queue->nextEntryToRead);
    Uint32 nextEntryToRead = (originalNextEntryToRead + 1) % SDL_arraysize(queue->entries);
    if(originalNextEntryToRead != queue->nextEntryToWrite) {
        if(SDL_CompareAndSwapAtomicU32(&queue->nextEntryToRead, originalNextEntryToRead, nextEntryToRead)) {
            WorkQueueEntry *entry = &queue->entries[originalNextEntryToRead];
            entry->callback(spall_ctx, spall_buffer, entry->data);
            SDL_AddAtomicU32(&queue->completionCount, 1);
        }
    } else {
        shouldSleep = true;
    }

    return shouldSleep;
}

void CompleteAllWorkerEntries(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, WorkQueue *queue)
{
    while(SDL_GetAtomicU32(&queue->completionGoal) != SDL_GetAtomicU32(&queue->completionCount)) {
        DoNextWorkEntry(spall_ctx, spall_buffer, queue);
    }

    SDL_SetAtomicU32(&queue->completionGoal, 0);
    SDL_SetAtomicU32(&queue->completionCount, 0);
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
    SDL_SetAtomicU32(&queue->completionGoal, 0);
    SDL_SetAtomicU32(&queue->completionCount, 0);

    queue->nextEntryToWrite = 0;
    SDL_SetAtomicU32(&queue->nextEntryToRead, 0);

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

#define SHADER_FORMAT_HLSL 0
#define SHADER_FORMAT_SPIRV 1
bool GetShaderStageFormatFromName(const char *shaderFile, SDL_GPUShaderStage *stage, Uint32 *format)
{
    char *c = (char*)shaderFile + SDL_strlen(shaderFile);
    while(c > shaderFile && *c != '.') c--;
    c--;
    while(c > shaderFile && *c != '.') c--;
    if(c == shaderFile) return false;
    c++;
    if(!SDL_strncmp(c, "vert", strlen("vert"))) {
        *stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    } else if(!SDL_strncmp(c, "frag", strlen("frag"))) {
        *stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    } else if(!SDL_strncmp(c, "comp", strlen("comp"))) {
        *stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
    } else return false;
    c += 4;

    if(*c != '.') return false;
    c++;
    if(!SDL_strncmp(c, "hlsl", strlen("hlsl"))) {
        *format = SHADER_FORMAT_HLSL;
    } else if(!SDL_strncmp(c, "spv", strlen("spv"))) {
        *format = SHADER_FORMAT_SPIRV;
    } else return false;

    return true;
}

#define CompileShader(gpu, shaderFile, info) \
    CompileShader_(gpu, SHADER_DIRECTORY shaderFile, info) 
bool CompileShader_(SDL_GPUDevice *gpu, const char *shaderFile, ShaderInfo *info)
{
    Uint64 currentFileTime;
    if(!GetLastWriteTime(shaderFile, &currentFileTime) || currentFileTime == info->fileTime) return true;
    info->fileTime = currentFileTime;

    SDL_GPUShaderStage stage;
    Uint32 format;
    if(!GetShaderStageFormatFromName(shaderFile, &stage, &format)) return false;

    size_t shaderFileSize;
    const char *shaderFileData = (const char*)SDL_LoadFile(shaderFile, &shaderFileSize);
    if(!shaderFileData) return false;

    size_t bytecodeSize;
    void *bytecode;
    switch(format) {
        case SHADER_FORMAT_HLSL: {
            SDL_ShaderCross_HLSL_Info hlslInfo = {
                .source = shaderFileData,
                .entrypoint = "main",
                .include_dir = 0,
                .defines = 0,
                .shader_stage = stage,
                .enable_debug = true,
                .name = 0,
                .props = 0,
            };
            bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlslInfo, &bytecodeSize);
            if(!bytecode) return false;
            SDL_free((void*)shaderFileData);
        } break;

        case SHADER_FORMAT_SPIRV: {
            bytecode = (void*)shaderFileData;
            bytecodeSize = shaderFileSize;
        } break;

        default: return false;
    }

    if(stage == SDL_SHADERCROSS_SHADERSTAGE_COMPUTE) {
        // TODO
        SDL_free(bytecode);
        return false;
    } else {
        SDL_ShaderCross_SPIRV_Info spvInfo = {
            .bytecode = (const Uint8*)bytecode,
            .bytecode_size = bytecodeSize,
            .entrypoint = "main",
            .shader_stage = stage,
            .enable_debug = true,
            .name = shaderFile,
            .props = 0,
        };

        info->meta = SDL_ShaderCross_ReflectGraphicsSPIRV(bytecode, bytecodeSize, 0);
        if(!info->meta) {
            SDL_free(bytecode);
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not get shader '%s' metadata", shaderFile);
            return false;
        }

        info->shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(gpu, &spvInfo, info->meta, 0);
        if(!info->shader) {
            SDL_free(bytecode);
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not compile shader '%s'", shaderFile);
            return false;
        }
    }

    return true;
}

void FreeShader(SDL_GPUDevice *gpu, ShaderInfo *shader)
{
    if(shader->meta) SDL_free(shader->meta);
    if(shader->shader) SDL_ReleaseGPUShader(gpu, shader->shader);
    shader->shader = 0;
    shader->meta = 0;
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

    if(!SDL_ShaderCross_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init sdl_shadercross: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("template", 800, 600, SDL_WINDOW_RESIZABLE);
    if(!ctx->window) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s", SDL_GetError());
        return false;
    }

    // temp: SDL_GPU_SHADERFORMAT_SPIRV | 
    ctx->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, 0);
    if(!ctx->gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create gpu device: %s", SDL_GetError());
        return false;
    }

    if(!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not claim window for gpu device: %s", SDL_GetError());
        return false;
    }

//#pragma message("TODO: SDR linear is not always supported, use: 'SDL_WindowSupportsGPUSwapchainComposition' to check")
//    if(!SDL_SetGPUSwapchainParameters(ctx->gpu, ctx->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC)) {
//        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not set gpu swapchain parameters: %s", SDL_GetError());
//        return false;
//    }

    ctx->swapchainTextureFormat = SDL_GetGPUSwapchainTextureFormat(ctx->gpu, ctx->window);

    if(!CompileShader(ctx->gpu, "shader.vert.hlsl", &ctx->vertexShader)) return false;
    if(!CompileShader(ctx->gpu, "shader.frag.hlsl", &ctx->fragmentShader)) return false;

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo = {
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = &(SDL_GPUColorTargetDescription){
                .format = ctx->swapchainTextureFormat,
            },
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_shader = ctx->vertexShader.shader,
        .fragment_shader = ctx->fragmentShader.shader,
    };
    
    pipelineCreateInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ctx->fillPipeline = SDL_CreateGPUGraphicsPipeline(ctx->gpu, &pipelineCreateInfo);
    if(!ctx->fillPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create fill pipeline");
        return false;
    }
    pipelineCreateInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
    ctx->linePipeline = SDL_CreateGPUGraphicsPipeline(ctx->gpu, &pipelineCreateInfo);
    if(!ctx->linePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create line pipeline");
        return false;
    }

    FreeShader(ctx->gpu, &ctx->vertexShader);
    FreeShader(ctx->gpu, &ctx->fragmentShader);

    const char *driver = SDL_GetGPUDeviceDriver(ctx->gpu);
    if(driver) {
        SDL_Log("Gpu device driver: %s", driver);
    }

    ctx->depthTextureFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
#define TestDepthTextureFormat(format) if(SDL_GPUTextureSupportsFormat(ctx->gpu, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) ctx->depthTextureFormat = format
    TestDepthTextureFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    TestDepthTextureFormat(SDL_GPU_TEXTUREFORMAT_D24_UNORM);
#undef TestDepthTextureFormat

    SDL_GetWindowSize(ctx->window, &ctx->windowWidth, &ctx->windowHeight);

    ctx->depthTexture = SDL_CreateGPUTexture(ctx->gpu, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .width = (Uint32)ctx->windowWidth,
        .height = (Uint32)ctx->windowHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .format = ctx->depthTextureFormat,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
    });
    if(!ctx->depthTexture) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create depth texture: %s", SDL_GetError());
        return false;
    }

    SDL_SetEventFilter(FilterSDL3Events, ctx);

    SDL_memset4(ctx->input.regularKeysUp, 0x01010101, sizeof(ctx->input.regularKeysUp)/4);
    ctx->input.mouseLeft.up = true; ctx->input.mouseMiddle.up = true; ctx->input.mouseRight.up = true;
    ctx->input.mouseX1.up = true; ctx->input.mouseX2.up = true;

    ctx->deltaTime = 0.0f;
    ctx->targetFPS = 60.0f;

    bool ok = InitWorkQueue(&ctx->spall_ctx, &ctx->workQueue, 2);

    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    return ok;
}

DLL_EXPORT void InitPartial(void *rawdata)
{
    (void) rawdata;
}

DLL_EXPORT void DeInitAll(void *rawdata)
{
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

    SDL_ShaderCross_Quit();

    SDL_ReleaseGPUGraphicsPipeline(ctx->gpu, ctx->fillPipeline);
    SDL_ReleaseGPUGraphicsPipeline(ctx->gpu, ctx->linePipeline);

    TTF_Quit();

    SDL_DestroyGPUDevice(ctx->gpu);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

DLL_EXPORT void DeInitPartial(void *rawdata)
{
    (void)rawdata;
}

void RenderAll(ProgramContext *ctx)
{
    Spall_BufferBegin(&ctx->spall_ctx, &ctx->spall_buffer, __FUNCTION__);

    SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(ctx->gpu);
    SDL_GPUTexture *swapchainTexture;
    bool ok = SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, ctx->window, &swapchainTexture, 0, 0);
    if(!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not aquire swapchain texture: %s", SDL_GetError());
        return;
    }

    if(swapchainTexture) {
        // render here

        SDL_FColor clearColor = {0.12f, 0.24f, 0.24f, 1.0f};
        SDL_GPUColorTargetInfo colorTarget = {
            .texture = swapchainTexture,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = clearColor,
            .store_op = SDL_GPU_STOREOP_STORE
        };
        
        SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, NULL);
        SDL_BindGPUGraphicsPipeline(renderPass, ctx->wireframeMode ? ctx->linePipeline : ctx->fillPipeline);
        SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);
    
        SDL_EndGPURenderPass(renderPass);
    }

    ok = SDL_SubmitGPUCommandBuffer(cmdBuf);
    if(!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not submit command buffer: %s", SDL_GetError());
        return;
    }

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

    ClearInput(ctx);

    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_EVENT_QUIT: {
                quit = true;
                SDL_SetAtomicInt(&ctx->targetFPSMissedCause, TargetFPSMissed_End);
            } break;

            case SDL_EVENT_KEY_DOWN: {
                if(event.key.key < 0xFF) {
                    if(ctx->input.regularKeysUp[event.key.key]) ctx->input.regularKeysPressed[event.key.key] = true;
                    ctx->input.regularKeysDown[event.key.key] = true;
                    ctx->input.regularKeysUp[event.key.key] = false;
                }
                //SDL_Log("key down: %u", event.key.key);
            } break;

            case SDL_EVENT_KEY_UP: {
                if(event.key.key < 0xFF) {
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

    if(ctx->input.regularKeysPressed[SDLK_W]) {
        ctx->wireframeMode = !ctx->wireframeMode;
    }

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

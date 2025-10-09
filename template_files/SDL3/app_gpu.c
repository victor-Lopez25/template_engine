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
    bool keyDown[0xFFF];
    bool keyUp[0xFFF];
    bool keyPressed[0xFFF];
    bool keyReleased[0xFFF];
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
    SDL_GPUShader *shader;
    SDL_ShaderCross_GraphicsShaderMetadata *meta;
    const char *filename;
    Uint64 fileTime;
} ShaderInfo;

typedef struct ProgramContext ProgramContext;

typedef struct {
    ProgramContext *ctx;
    bool (*CheckVertexInstanceSize)(Uint32);
    ShaderInfo vert;
    ShaderInfo frag;
    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_GPUGraphicsPipeline **pipeline;
    SDL_GPUColorTargetDescription *colorTargetDescs;
    SDL_Mutex *mutex;
    SDL_GPUColorTargetDescription colorDesc;
    SDL_AtomicInt recompiling;
} PipelineCompileContext;

// In case you want to disable this
bool CheckVertexInstanceSize_Default(Uint32 check) { (void)check; return true; }

#define VERTEX_INSTANCE(name, body) \
    typedef struct body name; \
    bool CheckVertexInstanceSize_##name(Uint32 check) { return check == (Uint32)sizeof(name); }

typedef struct {
    float x;
    float y;
} Vec2f;

VERTEX_INSTANCE(VertexInstance, {
    Vec2f position;
    Vec2f uv;
});

struct ProgramContext {
    ProgramInput input;
    SDL_Window *window;
    SDL_GPUDevice *gpu;

    SDL_GPUGraphicsPipeline *fillPipeline;
    SDL_GPUGraphicsPipeline *linePipeline;
    bool wireframeMode;

    SDL_GPUBuffer *vertexBuffer;

    PipelineCompileContext fillPipelineCompileCtx;
    PipelineCompileContext linePipelineCompileCtx;

    SDL_GPUTextureFormat swapchainTextureFormat;
    Sint32 windowWidth;
    Sint32 windowHeight;
    float deltaTime;
    float targetFPS;

    SDL_GPUTexture *catTexture;
    SDL_GPUSampler *sampler;
    SDL_GPUTransferBuffer *transferBuf;
    VertexInstance rectVertices[6];
    float rotationSpeed;
    bool rotating;

    SDL_GPUTextureFormat depthTextureFormat;
    SDL_GPUTexture *depthTexture;

    WorkQueue workQueue;

#define SPALL_BUFFER_SIZE 1024*1024
    SpallBuffer spall_buffer;
    SpallProfile spall_ctx;

    SDL_AtomicInt targetFPSMissedCause;
};

DLL_EXPORT size_t MemorySize(void) { return sizeof(ProgramContext); }

float Distance2f(float x, float y) {
    return SDL_sqrtf(x*x + y*y);
}

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

#define SHADER_FORMAT_HLSL 0
#define SHADER_FORMAT_SPIRV 1
bool GetShaderStageFormatFromName(const char *shaderFile, SDL_ShaderCross_ShaderStage *stage, Uint32 *format)
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

void FreeShader(SDL_GPUDevice *gpu, ShaderInfo *shader)
{
    if(shader->meta) SDL_free(shader->meta);
    shader->meta = 0;
    if(shader->shader) SDL_ReleaseGPUShader(gpu, shader->shader);
    shader->shader = 0;
}

bool CompileShader(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, SDL_GPUDevice *gpu, ShaderInfo *info)
{
    Spall_BufferBegin(spall_ctx, spall_buffer, __FUNCTION__);

    SDL_ShaderCross_ShaderStage stage;
    Uint32 format;
    if(!GetShaderStageFormatFromName(info->filename, &stage, &format)) {
        Spall_BufferEnd(spall_ctx, spall_buffer);
        return false;
    }

    size_t shaderFileSize;
    const char *shaderFileData = (const char*)SDL_LoadFile(info->filename, &shaderFileSize);
    if(!shaderFileData) {
        Spall_BufferEnd(spall_ctx, spall_buffer);
        return false;
    }

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
                .props = 0,
            };
            bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlslInfo, &bytecodeSize);
            SDL_free((void*)shaderFileData);
            if(!bytecode) {
                SDL_Log("ERROR: %s: %s", info->filename, SDL_GetError());
                Spall_BufferEnd(spall_ctx, spall_buffer);
                return false;
            }
        } break;

        case SHADER_FORMAT_SPIRV: {
            bytecode = (void*)shaderFileData;
            bytecodeSize = shaderFileSize;
        } break;

        default: return false;
    }

    SDL_ShaderCross_GraphicsShaderMetadata *meta;
    SDL_GPUShader *shader;
    if(stage == SDL_SHADERCROSS_SHADERSTAGE_COMPUTE) {
        // TODO
        SDL_free(bytecode);
        Spall_BufferEnd(spall_ctx, spall_buffer);
        return false;
    } else {
        SDL_ShaderCross_SPIRV_Info spvInfo = {
            .bytecode = (const Uint8*)bytecode,
            .bytecode_size = bytecodeSize,
            .entrypoint = "main",
            .shader_stage = stage,
            .props = 0,
        };

        meta = SDL_ShaderCross_ReflectGraphicsSPIRV(bytecode, bytecodeSize, 0);
        if(!meta) {
            SDL_free(bytecode);
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not get shader '%s' metadata", info->filename);
            Spall_BufferEnd(spall_ctx, spall_buffer);
            return false;
        }

        shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(gpu, &spvInfo, &meta->resource_info, 0);
        if(!shader) {
            SDL_free(bytecode);
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not compile shader '%s'", info->filename);
            SDL_free(meta);
            Spall_BufferEnd(spall_ctx, spall_buffer);
            return false;
        }
    }

    FreeShader(gpu, info);
    info->shader = shader;
    info->meta = meta;
    
    Spall_BufferEnd(spall_ctx, spall_buffer);

    return true;
}

SDL_GPUVertexElementFormat GPUVertexElementFormat_From_Metadata(SDL_ShaderCross_IOVarMetadata *meta);
Uint32 GetGPUVariableSize_From_Metadata(SDL_ShaderCross_IOVarMetadata *meta);

int PipelineFromShadersWork(SpallProfile *spall_ctx, SpallBuffer *spall_buffer, void *data)
{
    Spall_BufferBegin(spall_ctx, spall_buffer, __FUNCTION__);

    PipelineCompileContext *info = (PipelineCompileContext*)data;

    CompileShader(spall_ctx, spall_buffer, info->ctx->gpu, &info->vert);
    CompileShader(spall_ctx, spall_buffer, info->ctx->gpu, &info->frag);

    info->info.vertex_shader = info->vert.shader;
    info->info.fragment_shader = info->frag.shader;

    SDL_GPUVertexBufferDescription vertBufDesc[] = {
        {.slot = 0, .pitch = 0}
    };

    SDL_GPUVertexAttribute *attrs = 0;
    if(info->vert.meta->num_inputs > 0) {
        Uint32 currentOffset = 0;
        attrs = SDL_calloc(info->vert.meta->num_inputs, sizeof(SDL_GPUVertexAttribute));
        for(Uint32 i = 0; i < info->vert.meta->num_inputs; i++) {
            SDL_ShaderCross_IOVarMetadata *var = &info->vert.meta->inputs[i];
            attrs[i].location = var->location;
            attrs[i].format = GPUVertexElementFormat_From_Metadata(var);
            attrs[i].offset = currentOffset;
            currentOffset += GetGPUVariableSize_From_Metadata(var);
        }
        if(!info->CheckVertexInstanceSize(currentOffset)) {
            SDL_Log("Vertex input attributes changed for shader '%s' or in the non shader source ("__FILE__" unless defined elsewhere)", info->vert.filename);
            return 0;
        }
    
        vertBufDesc[0].pitch = currentOffset;

        info->info.vertex_input_state.num_vertex_buffers = 1;
        info->info.vertex_input_state.vertex_buffer_descriptions = vertBufDesc;
        info->info.vertex_input_state.num_vertex_attributes = info->vert.meta->num_inputs;
        info->info.vertex_input_state.vertex_attributes = attrs;
    } else {
        info->info.vertex_input_state.num_vertex_buffers = 0;
        info->info.vertex_input_state.vertex_buffer_descriptions = 0;
        info->info.vertex_input_state.num_vertex_attributes = 0;
        info->info.vertex_input_state.vertex_attributes = 0;
    }

    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(info->ctx->gpu, &info->info);
    if(attrs) SDL_free(attrs);

    SDL_GPUGraphicsPipeline *prevPipeline = *info->pipeline;
    if(SDL_TryLockMutex(info->mutex)) {
        *info->pipeline = pipeline;
        SDL_UnlockMutex(info->mutex);
    } else {
        SDL_LockMutex(info->mutex);
        *info->pipeline = pipeline;
        SDL_UnlockMutex(info->mutex);
    }
    SDL_ReleaseGPUGraphicsPipeline(info->ctx->gpu, prevPipeline);
    SDL_SetAtomicInt(&info->recompiling, 0);

    Spall_BufferEnd(spall_ctx, spall_buffer);

    return 0;
}

void PipelineFromShaders(PipelineCompileContext *ctx, bool initTime)
{
    Uint64 currentFileTimeVert;
    Uint64 currentFileTimeFrag;

    bool needRecompileVert = GetLastWriteTime(ctx->vert.filename, &currentFileTimeVert) && currentFileTimeVert != ctx->vert.fileTime;
    ctx->vert.fileTime = currentFileTimeVert;
    bool needRecompileFrag = GetLastWriteTime(ctx->frag.filename, &currentFileTimeFrag) && currentFileTimeFrag != ctx->frag.fileTime;
    ctx->frag.fileTime = currentFileTimeFrag;

    if(!initTime && !needRecompileVert && !needRecompileFrag) {
        return;
    }

    if(initTime) {
        PipelineFromShadersWork(&ctx->ctx->spall_ctx, &ctx->ctx->spall_buffer, ctx);
    } else {
        if(!SDL_SetAtomicInt(&ctx->recompiling, 1)) {
            AddWorkEntry(&ctx->ctx->workQueue, PipelineFromShadersWork, ctx);
        }
    }
}

#define InitPipelineCompileContext(prog_ctx, ctx, vert, frag, pipeline, vertInstanceStructureName) \
    InitPipelineCompileContext_(prog_ctx, ctx, SHADER_DIRECTORY vert, SHADER_DIRECTORY frag, pipeline, CheckVertexInstanceSize_##vertInstanceStructureName)
bool InitPipelineCompileContext_(ProgramContext *prog_ctx, PipelineCompileContext *ctx, const char *vert, const char *frag, SDL_GPUGraphicsPipeline **pipeline, bool (*CheckVertexInstanceSize)(Uint32))
{
    ctx->ctx = prog_ctx;
    ctx->vert.filename = vert;
    ctx->frag.filename = frag;
    ctx->pipeline = pipeline;
    if(CheckVertexInstanceSize) ctx->CheckVertexInstanceSize = CheckVertexInstanceSize;
    else ctx->CheckVertexInstanceSize = CheckVertexInstanceSize_Default;

    if(!ctx->colorTargetDescs) {
        ctx->colorDesc.format = prog_ctx->swapchainTextureFormat;
        ctx->info.target_info.num_color_targets = 1;
        ctx->info.target_info.color_target_descriptions = &ctx->colorDesc;
    }
    ctx->info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ctx->mutex = SDL_CreateMutex();
    return ctx->mutex != NULL;
}

void UploadVertices(ProgramContext *ctx, SDL_GPUCommandBuffer *cmdBuf)
{
    if(ctx->rotating) {
        ctx->rotationSpeed = SDL_PI_F*ctx->deltaTime;

        float cs = SDL_cosf(ctx->rotationSpeed);
        float sn = SDL_sinf(ctx->rotationSpeed);
        for(size_t i = 0; i < SDL_arraysize(ctx->rectVertices); i++) {
            float x = ctx->rectVertices[i].position.x;
            float y = ctx->rectVertices[i].position.y;
            ctx->rectVertices[i].position.x = x*cs - y*sn;
            ctx->rectVertices[i].position.y = x*sn + y*cs;
        }
    }

    void *transferMem = SDL_MapGPUTransferBuffer(ctx->gpu, ctx->transferBuf, true);
    SDL_memcpy(transferMem, ctx->rectVertices, sizeof(ctx->rectVertices));
    SDL_UnmapGPUTransferBuffer(ctx->gpu, ctx->transferBuf);

    SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    SDL_UploadToGPUBuffer(copyPass, &(SDL_GPUTransferBufferLocation){.transfer_buffer = ctx->transferBuf}, &(SDL_GPUBufferRegion){.buffer = ctx->vertexBuffer, .size = sizeof(ctx->rectVertices)}, true);
    SDL_EndGPUCopyPass(copyPass);
}

SDL_GPUTexture *GPU_UploadTexture(SDL_GPUDevice *gpu, SDL_GPUCopyPass *copyPass, const char *file, float *sizeRatio)
{
    SDL_assert(sizeRatio);
    SDL_Surface *surface_src = IMG_Load(file);
    if(!surface_src) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not load %s: %s", file, SDL_GetError());
        return 0;
    }

    SDL_Surface *surface = SDL_ConvertSurface(surface_src, SDL_PIXELFORMAT_RGBA32);
    if(!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not load %s: %s", file, SDL_GetError());
        SDL_DestroySurface(surface_src);
        return 0;
    }

    SDL_GPUTextureFormat format = SDL_GetGPUTextureFormatFromPixelFormat(surface->format);
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32)surface->w,
        .height = (Uint32)surface->h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    });

    size_t size = surface->w*surface->h*sizeof(Uint32);
    if(size > SDL_MAX_SINT32) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not upload %s image to gpu: Too large (size = %llu)", file, size);
        SDL_DestroySurface(surface_src);
        SDL_DestroySurface(surface);
        return false;
    }

    SDL_GPUTransferBuffer *texTransferBuf =
        SDL_CreateGPUTransferBuffer(gpu, &(SDL_GPUTransferBufferCreateInfo){
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (Uint32)size,
        });

    void *mem = SDL_MapGPUTransferBuffer(gpu, texTransferBuf, false);
    SDL_memcpy(mem, surface->pixels, size);
    SDL_UnmapGPUTransferBuffer(gpu, texTransferBuf);

    SDL_UploadToGPUTexture(copyPass, 
        &(SDL_GPUTextureTransferInfo){.transfer_buffer = texTransferBuf}, 
        &(SDL_GPUTextureRegion){.texture = texture, .w = (Uint32)surface->w, .h = (Uint32)surface->h, .d = 1},
        false);

    *sizeRatio = (float)surface->w/(float)surface->h;

    SDL_ReleaseGPUTransferBuffer(gpu, texTransferBuf);
    SDL_DestroySurface(surface_src);
    SDL_DestroySurface(surface);

    return texture;
}

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

    if(!SDL_ShaderCross_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not init sdl_shadercross: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("template", 800, 600, SDL_WINDOW_RESIZABLE);
    if(!ctx->window) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create window: %s", SDL_GetError());
        return false;
    }

    ctx->gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, 0);
    if(!ctx->gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create gpu device: %s", SDL_GetError());
        return false;
    }

    const char *driver = SDL_GetGPUDeviceDriver(ctx->gpu);
    if(driver) {
        SDL_Log("Gpu device driver: %s", driver);
        int sdl_image_version = IMG_Version();
        SDL_Log("SDL_image version: %d.%d.%d", sdl_image_version/1000000, (sdl_image_version/1000)%1000, sdl_image_version%1000);
    }

    if(!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not claim window for gpu device: %s", SDL_GetError());
        return false;
    }
    
    ctx->vertexBuffer = SDL_CreateGPUBuffer(ctx->gpu, &(SDL_GPUBufferCreateInfo){
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(ctx->rectVertices),
    });

//#pragma message("TODO: SDR linear is not always supported, use: 'SDL_WindowSupportsGPUSwapchainComposition' to check")
//    if(!SDL_SetGPUSwapchainParameters(ctx->gpu, ctx->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, SDL_GPU_PRESENTMODE_VSYNC)) {
//        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not set gpu swapchain parameters: %s", SDL_GetError());
//        return false;
//    }

    ctx->swapchainTextureFormat = SDL_GetGPUSwapchainTextureFormat(ctx->gpu, ctx->window);

    ctx->fillPipelineCompileCtx.info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

    InitPipelineCompileContext(ctx, &ctx->fillPipelineCompileCtx, 
        "shader.vert.spv", "shader.frag.spv", &ctx->fillPipeline, VertexInstance);
    PipelineFromShaders(&ctx->fillPipelineCompileCtx, true);
    if(!ctx->fillPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create fill pipeline");
        return false;
    }

    ctx->linePipelineCompileCtx.info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
    InitPipelineCompileContext(ctx, &ctx->linePipelineCompileCtx, 
        "shader.vert.spv", "shader.frag.spv", &ctx->linePipeline, VertexInstance);
    PipelineFromShaders(&ctx->linePipelineCompileCtx, true);
    if(!ctx->linePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Could not create line pipeline");
        return false;
    }

    ctx->sampler = SDL_CreateGPUSampler(ctx->gpu, &(SDL_GPUSamplerCreateInfo){0});
    float imageRatio = 1.0f;
    {
        SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(ctx->gpu);
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
        ctx->catTexture = GPU_UploadTexture(ctx->gpu, copyPass, "resources/cat.jpg", &imageRatio);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    }

    ctx->transferBuf = SDL_CreateGPUTransferBuffer(ctx->gpu, &(SDL_GPUTransferBufferCreateInfo){
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(ctx->rectVertices),
        });

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

    {
        float x, y;
        if(imageRatio > 1.0f) {
            x = 0.6f;
            y = 0.6f / imageRatio;
        } else {
            y = 0.6f;
            x = 0.6f / imageRatio;
        }

        /* first triangle:
            |\
            | \
            |__\
        */
        ctx->rectVertices[0].uv = (Vec2f){0.0, 0.0};
        ctx->rectVertices[0].position = (Vec2f){-x, y};
        ctx->rectVertices[1].uv = (Vec2f){0.0, 1.0};
        ctx->rectVertices[1].position = (Vec2f){-x, -y};
        ctx->rectVertices[2].uv = (Vec2f){1.0, 1.0};
        ctx->rectVertices[2].position = (Vec2f){x, -y};

        /* second triangle:
            \--|
             \ |
              \|
        */
        ctx->rectVertices[3].uv = (Vec2f){0.0, 0.0};
        ctx->rectVertices[3].position = (Vec2f){-x, y};
        ctx->rectVertices[4].uv = (Vec2f){1.0, 0.0};
        ctx->rectVertices[4].position = (Vec2f){x, y};
        ctx->rectVertices[5].uv = (Vec2f){1.0, 1.0};
        ctx->rectVertices[5].position = (Vec2f){x, -y};
        
        ctx->rotating = true;
    }

    SDL_SetEventFilter(FilterSDL3Events, ctx);

    SDL_memset4(ctx->input.keyUp, 0x01010101, sizeof(ctx->input.keyUp)/4);
    ctx->input.mouseLeft.up = true; ctx->input.mouseMiddle.up = true; ctx->input.mouseRight.up = true;
    ctx->input.mouseX1.up = true; ctx->input.mouseX2.up = true;

    ctx->deltaTime = 0.0f;
    ctx->targetFPS = 60.0f;

    bool ok = InitWorkQueue(&ctx->spall_ctx, &ctx->workQueue, 2);

    InitPartial(rawdata);

    Spall_BufferEnd(&ctx->spall_ctx, &ctx->spall_buffer);

    return ok;
}

void FreePipelineCompileContext(PipelineCompileContext *ctx)
{
    FreeShader(ctx->ctx->gpu, &ctx->vert);
    FreeShader(ctx->ctx->gpu, &ctx->frag);

    SDL_ReleaseGPUGraphicsPipeline(ctx->ctx->gpu, *ctx->pipeline);
    SDL_DestroyMutex(ctx->mutex);
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

    SDL_ShaderCross_Quit();

    FreePipelineCompileContext(&ctx->fillPipelineCompileCtx);
    FreePipelineCompileContext(&ctx->linePipelineCompileCtx);
    SDL_ReleaseGPUTransferBuffer(ctx->gpu, ctx->transferBuf);

    TTF_Quit();

    SDL_ReleaseGPUTexture(ctx->gpu, ctx->depthTexture);
    SDL_ReleaseGPUTexture(ctx->gpu, ctx->catTexture);
    SDL_ReleaseGPUSampler(ctx->gpu, ctx->sampler);
    SDL_ReleaseGPUBuffer(ctx->gpu, ctx->vertexBuffer);

    SDL_DestroyGPUDevice(ctx->gpu);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
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
        UploadVertices(ctx, cmdBuf);

        SDL_FColor clearColor = {0.12f, 0.24f, 0.24f, 1.0f};
        SDL_GPUColorTargetInfo colorTarget = {
            .texture = swapchainTexture,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = clearColor,
            .store_op = SDL_GPU_STOREOP_STORE
        };
        
        SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTarget, 1, NULL);
        SDL_BindGPUVertexBuffers(renderPass, 0, &(SDL_GPUBufferBinding){ .buffer = ctx->vertexBuffer }, 1);
        SDL_BindGPUFragmentSamplers(renderPass, 0, &(SDL_GPUTextureSamplerBinding){.texture = ctx->catTexture, .sampler = ctx->sampler}, 1);

        if(ctx->wireframeMode) {
            SDL_LockMutex(ctx->linePipelineCompileCtx.mutex);
            SDL_BindGPUGraphicsPipeline(renderPass, ctx->linePipeline);
        } else {
            SDL_LockMutex(ctx->fillPipelineCompileCtx.mutex);
            SDL_BindGPUGraphicsPipeline(renderPass, ctx->fillPipeline);
        }
        SDL_DrawGPUPrimitives(renderPass, SDL_arraysize(ctx->rectVertices), 1, 0, 0);
    
        SDL_EndGPURenderPass(renderPass);
        // NOTE: If this doesn't work, move it to after SDL_SubmitGPUCommandBuffer
        SDL_UnlockMutex(ctx->wireframeMode ? ctx->linePipelineCompileCtx.mutex : ctx->fillPipelineCompileCtx.mutex);
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
    SDL_memset4(ctx->input.keyPressed, 0, sizeof(ctx->input.keyPressed)/2);
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
                if(event.key.key < 0xFFF) {
                    if(ctx->input.keyUp[event.key.key]) ctx->input.keyPressed[event.key.key] = true;
                    ctx->input.keyDown[event.key.key] = true;
                    ctx->input.keyUp[event.key.key] = false;
                }
            } break;

            case SDL_EVENT_KEY_UP: {
                if(event.key.key < 0xFFF) {
                    if(ctx->input.keyDown[event.key.key]) ctx->input.keyReleased[event.key.key] = true;
                    ctx->input.keyUp[event.key.key] = true;
                    ctx->input.keyDown[event.key.key] = false;
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

    if(ctx->input.keyPressed[SDLK_W]) {
        ctx->wireframeMode = !ctx->wireframeMode;
    }

    if(ctx->input.keyPressed[SDLK_SPACE]) {
        ctx->rotating = !ctx->rotating;
    }

    PipelineFromShaders(&ctx->fillPipelineCompileCtx, false);
    PipelineFromShaders(&ctx->linePipelineCompileCtx, false);

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

SDL_GPUVertexElementFormat GPUVertexElementFormat_From_Metadata(SDL_ShaderCross_IOVarMetadata *meta)
{
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UNKNOWN) {
        SDL_assert(false);
        return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    }

    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT8) {
        SDL_assert(meta->vector_size == 2 || meta->vector_size == 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_BYTE2 + meta->vector_size/2 - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT8) {
        SDL_assert(meta->vector_size == 2 || meta->vector_size == 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2 + meta->vector_size/2 - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT16) {
        SDL_assert(meta->vector_size == 2 || meta->vector_size == 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2 + meta->vector_size/2 - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT16) {
        SDL_assert(meta->vector_size == 2 || meta->vector_size == 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_USHORT2 + meta->vector_size/2 - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT32) {
        SDL_assert(meta->vector_size >= 1 && meta->vector_size <= 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_INT + meta->vector_size - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT32) {
        SDL_assert(meta->vector_size >= 1 && meta->vector_size <= 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT + meta->vector_size - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT64) {
        SDL_assert(meta->vector_size == 1 || meta->vector_size == 2);
        return SDL_GPU_VERTEXELEMENTFORMAT_INT + meta->vector_size*2;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT64) {
        SDL_assert(meta->vector_size == 1 || meta->vector_size == 2);
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT + meta->vector_size*2;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT16) {
        SDL_assert(meta->vector_size == 2 || meta->vector_size == 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_HALF2 + meta->vector_size/2 - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT32) {
        SDL_assert(meta->vector_size >= 1 && meta->vector_size <= 4);
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT + meta->vector_size - 1;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT64) {
        SDL_assert(meta->vector_size == 1 || meta->vector_size == 2);
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT + meta->vector_size*2;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

Uint32 GetGPUVariableSize_From_Metadata(SDL_ShaderCross_IOVarMetadata *meta)
{
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UNKNOWN) {
        return 0;
    }

    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT8 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT8) {
        return meta->vector_size;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT16 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT16 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT16) {
        return sizeof(uint16_t)*meta->vector_size;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT32 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT32 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT32) {
        return sizeof(uint32_t)*meta->vector_size;
    }
    if(meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_INT64 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_UINT64 ||
       meta->vector_type == SDL_SHADERCROSS_IOVAR_TYPE_FLOAT64) {
        return sizeof(uint64_t)*meta->vector_size;
    }
    return 0;
}
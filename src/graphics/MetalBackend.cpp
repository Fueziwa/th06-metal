#include "MetalBackend.hpp"

#include "GameWindow.hpp"
#include "Supervisor.hpp"
#include "i18n.hpp"
#include "utils.hpp"

// metal-cpp single-header includes NS::Object, MTL::Device, CA::MetalLayer,
// and MTL4::CommandQueue. The single header pulls Foundation + Metal +
// QuartzCore together; this is the recommended integration approach.
//
// The *_PRIVATE_IMPLEMENTATION defines must be set before including the
// single header — they expand the inline objc_msgSend shims into real
// symbols in this TU. Define them in exactly one translation unit.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include "thirdparty/metal-cpp/SingleHeader/Metal.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <cstdio>

GfxInterface *MetalBackend::Create()
{
    MetalBackend *interface = new MetalBackend;

    SDL_Init(SDL_INIT_VIDEO);

    u32 flags = SDL_WINDOW_METAL | SDL_WINDOW_SHOWN;
    i32 height = GAME_WINDOW_HEIGHT_REAL;
    i32 width = GAME_WINDOW_WIDTH_REAL;
    i32 x = SDL_WINDOWPOS_UNDEFINED;
    i32 y = SDL_WINDOWPOS_UNDEFINED;

    if (g_Supervisor.cfg.windowed == 0)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window *window = SDL_CreateWindow(TH_WINDOW_TITLE, x, y, width, height, flags);
    interface->window = window;
    if (window == NULL)
    {
        utils::DebugPrint2("MetalBackend: SDL_CreateWindow failed: %s", SDL_GetError());
        delete interface;
        return nullptr;
    }

    if (!interface->Init())
    {
        delete interface;
        return nullptr;
    }

    utils::DebugPrint2("MetalBackend: using MTL::Device + MTL::CommandQueue");
    return interface;
}

bool MetalBackend::Init()
{
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

    // SDL_Metal_CreateView returns an opaque SDL_MetalView (a CAMetalLayer-backed
    // NSView on macOS). SDL_Metal_GetLayer unwraps it to the raw CAMetalLayer*.
    this->metalView = SDL_Metal_CreateView(this->window);
    if (this->metalView == NULL)
    {
        utils::DebugPrint2("MetalBackend: SDL_Metal_CreateView failed: %s", SDL_GetError());
        pool->drain();
        return false;
    }

    this->layer = static_cast<CA::MetalLayer *>(SDL_Metal_GetLayer(this->metalView));
    if (this->layer == nullptr)
    {
        utils::DebugPrint2("MetalBackend: SDL_Metal_GetLayer returned null");
        pool->drain();
        return false;
    }

    // Create the system default device and bind it to the layer. The layer's
    // device must match the device used to render its drawables.
    this->device = MTL::CreateSystemDefaultDevice();
    if (this->device == nullptr)
    {
        utils::DebugPrint2("MetalBackend: MTL::CreateSystemDefaultDevice returned null (Metal not available)");
        pool->drain();
        return false;
    }
    this->layer->setDevice(this->device);

    // Configure the CAMetalLayer for presentation. BGRA8Unorm matches the
    // pipeline color attachment format we'll create in M2. framebufferOnly=YES
    // enables GPU compression and is fine until ReadPixels (M5) needs readback.
    // displaySyncEnabled=YES caps presentation to the display refresh rate.
    this->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    this->layer->setMaximumDrawableCount(2);
    this->layer->setFramebufferOnly(true);
    this->layer->setDisplaySyncEnabled(true);
    // Note: metal-cpp's CA::MetalLayer binding does not expose setOpaque in
    // this version. The layer is opaque in practice for a fullscreen game;
    // direct-to-display eligibility is not needed for M1's clear-screen check.
    // drawableSize is set per-frame in SwapBuffers() from the window size, so
    // resize is handled without a separate windowDidResize: hook (the game
    // window is fixed at GAME_WINDOW_*_REAL).

    // Command queue. The goal doc targeted MTL4::CommandQueue, but MTL4 requires
    // macOS 26 (Tahoe); this dev machine is macOS 15 (Sequoia), where the
    // MTL4 selector throws at runtime. Per user direction, the port uses the
    // MTL (Metal 3) command queue, which runs on every supported macOS.
    // Migrating to MTL4 on a Tahoe target is a mechanical swap.
    this->commandQueue = this->device->newCommandQueue();
    if (this->commandQueue == nullptr)
    {
        utils::DebugPrint2("MetalBackend: newCommandQueue returned null");
        pool->drain();
        return false;
    }

    pool->drain();
    return true;
}

void MetalBackend::Exit()
{
    // metal-cpp objects are reference-counted via NS::Object; release() drops
    // our reference. The autorelease pool isn't required for release() itself
    // but keeps any transient objects created during teardown clean.
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

    // Tear down any in-flight frame state first — if Exit() is called mid-frame
    // (e.g. on error), the encoder/command buffer/drawable must be released
    // before the device and command queue go away.
    if (this->currentRenderEncoder != nullptr)
    {
        this->currentRenderEncoder->endEncoding();
        this->currentRenderEncoder->release();
        this->currentRenderEncoder = nullptr;
    }
    if (this->currentCommandBuffer != nullptr)
    {
        this->currentCommandBuffer->release();
        this->currentCommandBuffer = nullptr;
    }
    if (this->currentDrawable != nullptr)
    {
        this->currentDrawable->release();
        this->currentDrawable = nullptr;
    }

    if (this->commandQueue != nullptr)
    {
        this->commandQueue->release();
        this->commandQueue = nullptr;
    }
    if (this->device != nullptr)
    {
        this->device->release();
        this->device = nullptr;
    }
    // layer is owned by the NSView backing metalView; do not release it.
    this->layer = nullptr;

    if (this->metalView != nullptr)
    {
        SDL_Metal_DestroyView(this->metalView);
        this->metalView = nullptr;
    }
    if (this->window != nullptr)
    {
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
    }

    pool->drain();
}

// ============================================================================
// STUB(M0): All GfxInterface methods below are no-ops. They are tagged with
// STUB(Mx) comments indicating which milestone will implement each. The grep
// pattern `grep "STUB(M" src/graphics/MetalBackend.cpp` lists all deferred
// work.
// ============================================================================

void MetalBackend::SetFogRange(f32 nearPlane, f32 farPlane)
{
    // STUB(M5): SetFogRange — push to uniforms struct, consumed by ff.frag
}

void MetalBackend::SetFogColor(ZunColor color)
{
    // STUB(M5): SetFogColor — push to uniforms struct, consumed by ff.frag
}

void MetalBackend::ToggleVertexAttribute(u8 attr, bool enable)
{
    // STUB(M3): ToggleVertexAttribute — configure vertex descriptor / pipeline
}

void MetalBackend::SetAttributePointer(VertexAttributeArrays attr, std::size_t stride, void *ptr)
{
    // STUB(M3): SetAttributePointer — record CPU pointer + stride; copy to a
    // Shared storage mode MTLBuffer at Draw() time.
}

void MetalBackend::SetColorOp(TextureOpComponent component, ColorOp op)
{
    // STUB(M5): SetColorOp — map to shader uniform / blend state
}

void MetalBackend::SetTextureFactor(ZunColor factor)
{
    // STUB(M5): SetTextureFactor — push to uniforms struct
}

void MetalBackend::SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix)
{
    // STUB(M3): SetTransformMatrix — copy into per-frame uniform buffer
}

void MetalBackend::SetTextureFilter()
{
    // STUB(M4): SetTextureFilter — create/swap sampler state
}

void MetalBackend::GetViewport(u32 *viewport)
{
    // STUB(M5): GetViewport — return cached viewport (x,y,w,h)
}

void MetalBackend::GetDepthRange(f32 *depthRange)
{
    // STUB(M5): GetDepthRange — return cached near/far
}

void MetalBackend::SetViewport(i32 x, i32 y, i32 width, i32 height)
{
    // STUB(M5): SetViewport — set on render encoder at draw time
}

void MetalBackend::SetDepthRange(f32 nearPlane, f32 farPlane)
{
    // STUB(M5): SetDepthRange — setDepthClipPlane on render encoder
}

void MetalBackend::Enable(Capabilities cap)
{
    // STUB(M5): Enable — translate to depth/blend state on next pipeline
}

bool MetalBackend::HasError()
{
    // STUB(M0): HasError — no error tracking yet
    return false;
}

void MetalBackend::SetBlendMode(BlendMode mode)
{
    // STUB(M5): SetBlendMode — pipeline blend state
}

void MetalBackend::SetDepthMask(bool enable)
{
    // STUB(M5): SetDepthMask — depth stencil state
}

void MetalBackend::SetDepthFunc(DepthFunc func)
{
    // STUB(M5): SetDepthFunc — depth stencil state
}

void MetalBackend::SetClearDepth(f32 depth)
{
    this->clearDepth = depth;
}

void MetalBackend::SetClearColor(f32 r, f32 g, f32 b, f32 a)
{
    this->clearColor[0] = r;
    this->clearColor[1] = g;
    this->clearColor[2] = b;
    this->clearColor[3] = a;
}

void MetalBackend::Clear(u32 clearBits)
{
    // M1: defer the actual clear to SwapBuffers(). The game calls Clear()
    // multiple times per frame — once with color|depth at frame start, and
    // mid-frame depth-only clears during the draw chain. Metal can't cheaply
    // clear mid-pass, so we OR the requested bits and apply them once at pass
    // open. This is sufficient for M1's "solid color in window" success
    // criterion. M5 will revisit mid-frame clears if the game needs them.
    this->pendingClearBits |= clearBits;
}

GfxTextureHandle MetalBackend::CreateTexture()
{
    // STUB(M4): CreateTexture — allocate MTLTexture, return id handle
    GfxTextureHandle handle(this->nextTextureId++);
    return handle;
}

void MetalBackend::BindTexture(GfxTextureHandle handle)
{
    // STUB(M4): BindTexture — record for next argument table set
}

void MetalBackend::DeleteTexture(GfxTextureHandle handle)
{
    // STUB(M4): DeleteTexture — release MTLTexture
}

void MetalBackend::SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type, const void *data)
{
    // STUB(M4): SetTextureImage — create texture with pixel format mapping
}

void MetalBackend::SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height, const void *data)
{
    // STUB(M5): SetTextureSubImage — blit/replace region
}

void MetalBackend::ReadPixels(i32 x, i32 y, i32 width, i32 height, const void *pixels)
{
    // STUB(M5): ReadPixels — getBytes on drawable texture
}

void MetalBackend::Draw(PrimitiveType type, i32 start, i32 count)
{
    // STUB(M3): Draw — drawPrimitives on render encoder
}

void MetalBackend::SwapBuffers()
{
    // M1: open the frame's render pass (clearing with the cached values), then
    // immediately close and present. No draws are recorded yet — this just
    // verifies the full present pipeline: nextDrawable -> render pass (clear)
    // -> endEncoding -> presentDrawable -> commit. M3+ will move pass opening
    // earlier so draws land inside the encoder.
    NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

    // Keep the layer's drawableSize in sync with the window. The game window
    // is fixed-size, but this is cheap and handles fullscreen transitions.
    int w = 0, h = 0;
    SDL_Metal_GetDrawableSize(this->window, &w, &h);
    if (w > 0 && h > 0)
    {
        this->layer->setDrawableSize(CGSizeMake(w, h));
    }

    // Acquire the drawable for this frame. nextDrawable blocks on the CPU
    // until a drawable is available from the pool (max 2 in flight).
    this->currentDrawable = this->layer->nextDrawable();
    if (this->currentDrawable == nullptr)
    {
        utils::DebugPrint2("MetalBackend: nextDrawable returned null");
        this->pendingClearBits = 0;
        pool->drain();
        return;
    }

    this->currentCommandBuffer = this->commandQueue->commandBuffer();
    this->currentCommandBuffer->setLabel(NS::String::string("th06 frame", NS::UTF8StringEncoding));

    MTL::RenderPassDescriptor *desc = MTL::RenderPassDescriptor::renderPassDescriptor();
    desc->setRenderTargetWidth((NS::UInteger)w);
    desc->setRenderTargetHeight((NS::UInteger)h);

    // Color attachment 0 = the drawable's texture. Load=Clear bakes the clear
    // value into the pass; Store=Store writes the result back for presentation.
    // If no color clear was requested this frame, Load=Load preserves prior
    // contents (M1 always clears color, so this branch is mostly defensive).
    MTL::RenderPassColorAttachmentDescriptor *colorAtt = desc->colorAttachments()->object(0);
    colorAtt->setTexture(this->currentDrawable->texture());
    if (this->pendingClearBits & CLEAR_COLOR_BUFFER)
    {
        colorAtt->setLoadAction(MTL::LoadActionClear);
        colorAtt->setClearColor(MTL::ClearColor::Make(this->clearColor[0], this->clearColor[1],
                                                        this->clearColor[2], this->clearColor[3]));
    }
    else
    {
        colorAtt->setLoadAction(MTL::LoadActionLoad);
    }
    colorAtt->setStoreAction(MTL::StoreActionStore);

    // Depth attachment. M1 has no depth-stencil pipeline yet, but the game
    // clears depth every frame, so allocate a Private depth texture sized to
    // the drawable. M2 will pair this with a real MTLDepthStencilState.
    // STUB(M2): replace this throwaway depth texture with a persistent one
    // owned by the backend and resized with the drawable.
    if (this->pendingClearBits & CLEAR_DEPTH_BUFFER)
    {
        MTL::TextureDescriptor *depthDesc = MTL::TextureDescriptor::alloc()->init();
        depthDesc->setTextureType(MTL::TextureType2D);
        depthDesc->setPixelFormat(MTL::PixelFormatDepth32Float);
        depthDesc->setWidth((NS::UInteger)w);
        depthDesc->setHeight((NS::UInteger)h);
        depthDesc->setStorageMode(MTL::StorageModePrivate);
        depthDesc->setUsage(MTL::TextureUsageRenderTarget);
        MTL::Texture *depthTex = this->device->newTexture(depthDesc);
        depthTex->setLabel(NS::String::string("th06 depth (M1 throwaway)", NS::UTF8StringEncoding));
        depthDesc->release();

        MTL::RenderPassDepthAttachmentDescriptor *depthAtt = desc->depthAttachment();
        depthAtt->setTexture(depthTex);
        depthAtt->setLoadAction(MTL::LoadActionClear);
        depthAtt->setStoreAction(MTL::StoreActionDontCare);
        depthAtt->setClearDepth(this->clearDepth);
        // The depth texture is released when desc is drained below; it lives
        // only for the duration of this render pass.
        depthTex->release();
    }

    this->currentRenderEncoder = this->currentCommandBuffer->renderCommandEncoder(desc);
    this->currentRenderEncoder->setLabel(NS::String::string("th06 main pass", NS::UTF8StringEncoding));
    this->currentRenderEncoder->endEncoding();
    this->currentRenderEncoder->release();
    this->currentRenderEncoder = nullptr;

    desc->release();

    this->currentCommandBuffer->presentDrawable(this->currentDrawable);
    this->currentCommandBuffer->commit();

    // Release per-frame state. The drawable's backing texture is now owned by
    // the command buffer until it completes; releasing our reference here lets
    // the layer recycle the drawable.
    this->currentCommandBuffer->release();
    this->currentCommandBuffer = nullptr;
    this->currentDrawable->release();
    this->currentDrawable = nullptr;

    // Reset for the next frame.
    this->pendingClearBits = 0;

    pool->drain();
}

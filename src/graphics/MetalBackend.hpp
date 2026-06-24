#pragma once

#include "GfxInterface.hpp"
#include <SDL2/SDL.h>

// Forward declarations for metal-cpp types — avoids pulling every Metal header
// into every TU that includes this file (it's included via GameWindow.cpp).
namespace MTL
{
class Device;
class CommandQueue;
class CommandBuffer;
class RenderCommandEncoder;
class RenderPassDescriptor;
}
namespace CA
{
class MetalLayer;
class MetalDrawable;
}

// metal-cpp Metal backend for th06.
//
// NOTE: The goal doc originally targeted Metal 4 (MTL4::CommandQueue and the
// MTL4 encoder APIs). MTL4 requires macOS 26 (Tahoe). The development machine
// is macOS 15 (Sequoia), where the MTL4 selectors don't exist and throw at
// runtime. Per user direction, the port uses the MTL (Metal 3) API surface
// instead — this runs on every supported macOS and metal-cpp exposes both.
// No th06 feature needs MTL4-only capabilities (mesh shaders, raytracing,
// tier-2 argument tables). Migration to MTL4 on a Tahoe target would be a
// mechanical swap.
//
// M0 (this milestone): everything is stubbed. Create() wires the SDL2 window
// with SDL_WINDOW_METAL, obtains the CAMetalLayer from SDL, creates the
// MTL::Device and the MTL::CommandQueue. All GfxInterface methods are no-ops
// tagged with STUB(M0) so they can be located and filled in during later
// milestones.
struct MetalBackend : GfxInterface
{
    static GfxInterface *Create();

    bool Init();
    virtual void Exit();
    ~MetalBackend() override
    {
        Exit();
    }

    virtual void SetFogRange(f32 nearPlane, f32 farPlane) override;
    virtual void SetFogColor(ZunColor color) override;
    virtual void ToggleVertexAttribute(u8 attr, bool enable) override;
    virtual void SetAttributePointer(VertexAttributeArrays attr, std::size_t stride, void *ptr) override;
    virtual void SetColorOp(TextureOpComponent component, ColorOp op) override;
    virtual void SetTextureFactor(ZunColor factor) override;
    virtual void SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix) override;

    virtual void SetTextureFilter() override;

    virtual void GetViewport(u32 *viewport) override;
    virtual void GetDepthRange(f32 *depthRange) override;
    virtual void SetViewport(i32 x, i32 y, i32 width, i32 height) override;
    virtual void SetDepthRange(f32 nearPlane, f32 farPlane) override;

    virtual void Enable(Capabilities cap) override;

    virtual bool HasError() override;

    virtual void SetBlendMode(BlendMode mode) override;
    virtual void SetDepthMask(bool enable) override;
    virtual void SetDepthFunc(DepthFunc func) override;

    virtual void SetClearDepth(f32 depth) override;
    virtual void SetClearColor(f32 r, f32 g, f32 b, f32 a) override;
    virtual void Clear(u32 clearBits) override;

    virtual GfxTextureHandle CreateTexture() override;
    virtual void BindTexture(GfxTextureHandle handle) override;
    virtual void DeleteTexture(GfxTextureHandle handle) override;
    virtual void SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type,
                                 const void *data) override;

    virtual void SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height, const void *data) override;

    virtual void ReadPixels(i32 x, i32 y, i32 width, i32 height, const void *pixels) override;

    virtual void Draw(PrimitiveType type, i32 start, i32 count) override;
    virtual void SwapBuffers() override;

  private:
    SDL_Window *window = nullptr;
    SDL_MetalView metalView = nullptr;
    CA::MetalLayer *layer = nullptr;
    MTL::Device *device = nullptr;
    MTL::CommandQueue *commandQueue = nullptr;

    // Per-frame presentation state. The frame is opened lazily in SwapBuffers()
    // (which acquires the drawable, opens a render pass with the cached clear
    // values, and immediately closes+presents). M1 only clears — no draw calls
    // are recorded yet. M3+ will open the pass earlier (in Clear or a dedicated
    // BeginFrame) so draws can be recorded into the encoder before present.
    CA::MetalDrawable *currentDrawable = nullptr;
    MTL::CommandBuffer *currentCommandBuffer = nullptr;
    MTL::RenderCommandEncoder *currentRenderEncoder = nullptr;

    // Cached clear values. SetClearColor/SetClearDepth store here; SwapBuffers()
    // bakes them into the render pass descriptor's load action. Stored as raw
    // floats because MTL::ClearColor is a struct that needs the full metal-cpp
    // header.
    f32 clearColor[4] = {0, 0, 0, 1};
    double clearDepth = 1.0;
    // OR of CLEAR_* bits requested since the last present. SwapBuffers() only
    // clears attachments whose bit is set; the game's per-frame
    // Clear(color|depth) sets both, and mid-frame depth-only clears OR in.
    u32 pendingClearBits = 0;

    // Next texture id to hand out from CreateTexture(). M4 will replace this
    // with a real texture pool keyed by id.
    u32 nextTextureId = 1;
};

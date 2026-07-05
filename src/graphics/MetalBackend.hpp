#pragma once

#include "GfxInterface.hpp"
#include <SDL2/SDL.h>

// Forward declaration for the metal-cpp NS::UInteger typedef. The full
// metal-cpp single header is only included in MetalBackend.cpp (it's heavy
// and pulls in Foundation + Metal + QuartzCore); the header just needs the
// unsigned int type for the depth texture size members.
namespace NS
{
typedef unsigned long UInteger;
}

// Forward declarations for metal-cpp types — avoids pulling every Metal header
// into every TU that includes this file (it's included via GameWindow.cpp).
namespace MTL
{
class Device;
class CommandQueue;
class CommandBuffer;
class RenderCommandEncoder;
class RenderPassDescriptor;
class RenderPipelineState;
class DepthStencilState;
class DepthStencilDescriptor;
class RenderPipelineDescriptor;
class VertexDescriptor;
class Library;
class Function;
class Buffer;
class Texture;
class DepthStencilDescriptor;
}
namespace CA
{
class MetalLayer;
class MetalDrawable;
}

// Uniform buffer layout shared between MetalBackend.cpp and the embedded MSL
// shaders. Bound at buffer index 0 in the render encoder. Std140/PoD —
// CPU writes into a Shared storage mode MTLBuffer, GPU reads directly.
//
// Matches the GLSL ff.vert/ff.frag uniforms (10 logical uniforms collapsed
// where the GL backend used separate glUniform* calls).
struct MetalUniforms
{
    ZunMatrix modelviewMatrix;       // UNIFORM_MODELVIEW
    ZunMatrix projectionMatrix;      // UNIFORM_PROJECTION
    ZunMatrix textureMatrix;         // UNIFORM_TEXTURE_MATRIX
    ZunVec4   envDiffuse;            // UNIFORM_ENV_DIFFUSE  (SetTextureFactor)
    ZunVec4   fogColor;              // UNIFORM_FOG_COLOR
    i32       useTexCoords;          // UNIFORM_TEX_COORD_FLAG (bool as int)
    i32       useDiffuse;            // UNIFORM_DIFFUSE_FLAG  (bool as int, currently unused by frag shader)
    i32       colorOp;               // UNIFORM_COLOR_OP (0=MODULATE,1=ADD,2=REPLACE)
    f32       fogNear;               // UNIFORM_FOG_NEAR
    f32       fogFar;                // UNIFORM_FOG_FAR
};

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

    // ---- M2: persistent pipeline / depth / uniform state ----
    // Built once in Init() after the device is available. Released in Exit().
    // The render pipeline bakes vertex layout + shaders + color attachment
    // format; the depth-stencil state bakes depth test/write/func. The uniform
    // buffer is the CPU-visible backing for the shader's uniforms struct; the
    // game updates it piecemeal via SetTransformMatrix/SetFog*/etc.
    MTL::Library *shaderLibrary = nullptr;
    MTL::Function *vertexFunction = nullptr;
    MTL::Function *fragmentFunction = nullptr;
    MTL::RenderPipelineState *pipelineState = nullptr;
    MTL::DepthStencilState *depthStencilState = nullptr;
    MTL::Buffer *uniformBuffer = nullptr;
    // Persistent depth texture, resized with the drawable. Replaces the
    // throwaway per-frame allocation in M1's SwapBuffers.
    MTL::Texture *depthTexture = nullptr;
    NS::UInteger depthTextureWidth = 0;
    NS::UInteger depthTextureHeight = 0;

    // CPU-side mirror of `uniformBuffer`'s contents. SetXxx() writes here,
    // SwapBuffers() flushes to the GPU buffer (or the GPU reads via shared
    // memory directly). Initialized to identity matrices / sensible defaults
    // in Init().
    MetalUniforms uniforms{};

    // Creates (or recreates) the depth texture if the drawable size has
    // changed since the last call. No-op if the size matches.
    void EnsureDepthTexture(NS::UInteger width, NS::UInteger height);

    // M2: build shader library + pipeline + depth-stencil + uniform buffer.
    // Returns false on any failure; Init() treats that as fatal.
    bool InitPipeline();

    // Copy the CPU-side `uniforms` struct into `uniformBuffer` (Shared storage
    // mode — contents() is a CPU pointer). Called at frame open and from Init
    // to seed initial state.
    void FlushUniforms();
};

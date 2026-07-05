#pragma once
#include "asset_manager.hpp"
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "glm/mat4x4.hpp"
#include "globals.hpp"
#include "gpu_timer.hpp"
#include "mesh.hpp"
#include "render_target.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct GpuImageData {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channelCount = 4;
    // Raw OpenGL readback is bottom-up; set when data has NOT been row-flipped.
    bool bottomUp = false;
};

using PixelReadbackCallback = std::function<void(const GpuImageData&)>;

class GraphicsSubsystem;
class ShaderProgram;
class Renderer;

struct RenderCommand {
    MeshHandle mesh;
    // Material* material; // TODO: currently material is coupled with mesh
    glm::mat4 transform;
};

// Camera/target override consumed by the scene passes (Skybox, Sun,
// VoxelChunk) so PlanarReflectionPass can re-render the world from a mirrored
// camera into an offscreen RenderTarget. Renderer::viewOverride is null while
// rendering the main view.
struct RenderViewOverride {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec3 eyePos{ 0.0f };
    RenderTarget* target = nullptr;// render here instead of sceneRT
    // World-space clip plane (n, d): fragments with dot(n, P) + d < 0 are
    // discarded. All-zero disables clipping (dot == 0 is kept).
    glm::vec4 clipPlane{ 0.0f };
    bool flipCull = false;// mirrored views reverse triangle winding
};

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#endif

class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) = 0;
};

class ShadowPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~ShadowPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue);
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _frameUniformBuf = nullptr;// light-space viewProj
    WGPUBuffer _drawUniformBuf = nullptr;// per-draw model, dynamic offset
    uint32_t _drawSlotCapacity = 0;
    // Directional shadow map. The view is published to Renderer each frame
    // for ForwardOpaquePass to sample; this pass owns both handles.
    WGPUTexture _shadowTex = nullptr;
    WGPUTextureView _shadowView = nullptr;
#endif
};

// Renders MeshType::PRIM meshes from the opaque queue. Under WebGPU, TERRAIN
// meshes are drawn too, via a non-tessellated heightmap-displacement pipeline
// mirroring the GLES/WebGL2 fallback (terrain_simple.vert + terrain.frag);
// VOXEL is handled by VoxelChunkPass.
class ForwardOpaquePass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~ForwardOpaquePass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUBindGroup _getOrCreateTexBG(uint32_t texID);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    // Heightmap-displacement terrain (TERRAIN_WGSL); shares group 0/1 layouts
    // with _pipeline so _uniformBG and the texture BG cache are reused.
    WGPURenderPipeline _terrainPipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, camera, light) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, surface params) — dynamic-offset slot per mesh,
    // mirrors VoxelChunkPass's pattern (see voxel_chunk_pass.cpp).
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;

    // AssetManager::GetDefaultTextures() is GL-only; this pass owns its own
    // fallback texture/sampler for meshes with no base map under WebGPU.
    WGPUTexture _whiteTex = nullptr;
    WGPUSampler _sampler = nullptr;
    // `tex` records the WGPUTexture each bind group was built from so the
    // cache self-invalidates when GfxFactory::UpdateTexture2D recreates the
    // texture under the same synthetic ID (see GPUCanvasPass::CachedTexBG).
    struct CachedTexBG {
        WGPUBindGroup bg = nullptr;
        WGPUTexture tex = nullptr;
    };
    std::unordered_map<uint32_t, CachedTexBG> _texBGCache;

    // Shadow-map bind group (group 2): rebuilt only when the view published
    // by ShadowPass on the Renderer changes.
    WGPUBindGroupLayout _shadowBGL = nullptr;
    WGPUSampler _shadowSampler = nullptr;
    WGPUBindGroup _shadowBG = nullptr;
    WGPUTextureView _shadowBGSource = nullptr;
#endif
};

class DeferredGeometryPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class DeferredLightingPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// For particles, world UI
class TransparentPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class MSAAResolvePass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class WorldCanvasPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class CanvasPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Final composite blit: single post_composite shader pass. Every effect is an
// independent toggle, applied in a fixed order (see post_composite.frag):
// UV distortion -> CA fetch -> tonemap -> LDR stylize -> gamma encode.
class PostProcessPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~PostProcessPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool tonemapEnabled = true;
    float exposure = 0.5f;

    bool caEnabled = false;
    float caStrength = 0.005f;

    bool crtEnabled = false;
    bool vhsEnabled = false;
    bool gradingEnabled = false;
    bool posterizeEnabled = false;
    bool sobelEnabled = false;
    bool edgesEnabled = false;
    bool vignetteEnabled = false;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat swapchainFormat);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUSampler _sampler = nullptr;
    // Texture bind group is recreated whenever sceneRT's texture changes
    // (e.g. on resize); cached here to avoid rebuilding it every frame.
    WGPUBindGroup _texBG = nullptr;
    WGPUTexture _texBGSource = nullptr;
#endif
};

// TODO: rename this
class UIPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Flat billboard quad at the light source position — reads SunComponent for visual params.
class SunPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SunPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUBuffer _vertexBuf = nullptr;
#endif
};

// Gradient sky rendered at depth=1 (behind everything).  Matches VX's Skybox.
class SkyboxPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SkyboxPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    glm::vec3 skyColor = glm::vec3(0.686f, 0.933f, 0.933f);// VX COLOR_MINT_GREEN
    glm::vec3 horizonColor = glm::vec3(1.000f, 0.980f, 0.804f);// VX COLOR_LEMON_CREAM

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUBuffer _vertexBuf = nullptr;
#endif
};

// Renders MeshType::VOXEL meshes from the opaque queue using the voxel shader.
class VoxelChunkPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~VoxelChunkPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    int paletteIndex = 4;// 0-5; 4 = VX Palette 5 (soft cool blue-grey)

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(
        WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount, WGPUCullMode cullMode
    );
    // Grows _drawUniformBuf/_uniformBG to fit at least drawCount dynamic-offset slots.
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, light, fog, camera) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, normalMat) — one slot per mesh, selected via dynamic offset.
    // Sized lazily; all slots are written in a single call before any draw is
    // recorded each frame, since wgpuQueueWriteBuffer ordering is relative to
    // Queue::Submit, not to render-pass recording order.
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;
#endif
};

// Renders MeshType::PRIM water meshes tagged via material renderQueue.
class WaterPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~WaterPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    float time = 0.0f;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    void _ensureDrawCapacity(uint32_t drawCount);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, camera, light, time) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, water shading params) — dynamic-offset slot per
    // mesh, mirrors ForwardOpaquePass/VoxelChunkPass's pattern.
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;

    // Planar-reflection texture (group 1). When PlanarReflectionPass is
    // inactive a 1x1 black fallback is bound instead; the per-draw
    // reflection strength is zeroed so the sample is never visible. The bind
    // group is rebuilt whenever the bound WGPUTexture changes (RT resize).
    WGPUBindGroupLayout _reflBGL = nullptr;
    WGPUSampler _reflSampler = nullptr;
    WGPUTexture _reflFallbackTex = nullptr;
    WGPUBindGroup _reflBG = nullptr;
    WGPUTexture _reflBGSource = nullptr;
#endif
};

// Re-renders the world (Skybox + Sun + VoxelChunk content) mirrored about a
// reflective material's plane into an offscreen RenderTarget. Consumed by
// WaterPass today; a future mirror surface only needs planarReflection=true
// on its material plus a shader that samples GetReflectionRT().
//
// The reflection plane is derived from the reflective object's transform:
// the plane through its position with its up (+Y) axis as normal. One plane
// is supported per frame — the first draw whose material opts in wins.
// PRIM opaque meshes (ForwardOpaquePass content) are not yet re-rendered.
class PlanarReflectionPass : public RenderPass {
public:
    PlanarReflectionPass();
    ~PlanarReflectionPass() override;
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    // Reflection RT resolution relative to the window's physical size.
    float resolutionScale = 0.5f;

    // True when a reflection was rendered this frame (reflective material
    // found and the camera is on the reflective side of the plane).
    bool IsActive() const {
        return _active;
    }
    RenderTarget* GetReflectionRT() const {
        return _rt.get();
    }
    // proj * mirroredView of the frame this RT was rendered with; project a
    // world position with it to get the RT sample position in clip space.
    const glm::mat4& GetReflectionViewProj() const {
        return _reflViewProj;
    }

private:
    std::unique_ptr<RenderTarget> _rt;
    glm::mat4 _reflViewProj{ 1.0f };
    bool _active = false;

    // Private sub-pass instances rather than the RenderGraph's: under WebGPU
    // every pass writes its single per-frame uniform buffer via
    // wgpuQueueWriteBuffer, whose ordering is relative to Queue::Submit — so
    // executing the graph's own instances a second time per frame would make
    // both views observe only the last-written uniforms. Separate instances
    // own separate buffers (and, for VoxelChunkPass, a front-face-culling
    // pipeline for the mirrored winding).
    std::unique_ptr<SkyboxPass> _skybox;
    std::unique_ptr<SunPass> _sun;
    std::unique_ptr<VoxelChunkPass> _voxel;
};

// Pyramid bloom: threshold → downsample → upsample → composite.
class BloomPass : public RenderPass {
public:
    ~BloomPass() override;
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool enabled = false;
    float threshold = 1.0f;// VX: brightness.frag hard cutoff
    float bloomStrength = 0.08f;// VX: u_bloom_strength

private:
    static constexpr int MIP_LEVELS = 5;

    struct MipRT {
        GLuint fbo = 0;
        GLuint tex = 0;
        int w = 0;
        int h = 0;
    };

    MipRT _mips[MIP_LEVELS];
    GLuint _tempFBO = 0;
    GLuint _tempTex = 0;
    bool _initialized = false;
    int _lastW = 0, _lastH = 0;

    void InitMips(int w, int h);
    void DrawScreenQuad(GLuint screenVAO);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // WebGPU bloom mirrors the GL path exactly: a MIP_LEVELS mip pyramid with
    // threshold -> downsample chain -> additive upsample chain -> composite.
    // Avoids reading sceneRT's own texture while it's bound as the composite
    // pass's render attachment by first copying it into _snapshotTex (requires
    // CopySrc on sceneRT's texture, see gpu_render_target.cpp); the threshold
    // pass samples that same snapshot.
    void _initGPU(WGPUDevice device, WGPUQueue queue, uint32_t sceneSampleCount);
    void _resizeGPU(int width, int height);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPUSampler _sampler = nullptr;

    WGPURenderPipeline _threshPipeline = nullptr;
    WGPURenderPipeline _downPipeline = nullptr;
    WGPURenderPipeline _upPipeline = nullptr;// additive blend
    WGPURenderPipeline _compPipeline = nullptr;

    WGPUBindGroupLayout _threshBGL = nullptr;
    WGPUBindGroupLayout _downBGL = nullptr;
    WGPUBindGroupLayout _upBGL = nullptr;
    WGPUBindGroupLayout _compBGL = nullptr;

    WGPUBuffer _threshUniformBuf = nullptr;// threshold, rewritten each frame
    WGPUBuffer _compUniformBuf = nullptr;// bloomStrength, rewritten each frame
    // One texelSize uniform per down/up step; content depends only on the mip
    // sizes, so written once per resize. Indices 1..MIP_LEVELS-1 are used.
    WGPUBuffer _downUniformBuf[MIP_LEVELS] = {};
    WGPUBuffer _upUniformBuf[MIP_LEVELS] = {};

    // GPU mip chain mirroring the GL _mips pyramid; level 0 is half-res, each
    // subsequent level halves again. Level 0 also holds the final accumulated
    // bloom after the upsample chain (what the composite samples).
    struct GpuMip {
        WGPUTexture tex = nullptr;
        WGPUTextureView view = nullptr;
        int w = 0, h = 0;
    };
    GpuMip _gpuMips[MIP_LEVELS];

    // Full-resolution copy of sceneRT, refreshed each frame before composite.
    WGPUTexture _snapshotTex = nullptr;
    WGPUTextureView _snapshotView = nullptr;

    WGPUBindGroup _threshBG = nullptr;// reads _snapshotTex, writes mip[0]
    WGPUBindGroup _downBG[MIP_LEVELS] = {};// _downBG[i] reads mip[i-1], writes mip[i]
    WGPUBindGroup _upBG[MIP_LEVELS] = {};// _upBG[i] reads mip[i], writes mip[i-1]
    WGPUBindGroup _compBG = nullptr;// reads _snapshotTex + mip[0]

    int _gpuWidth = 0, _gpuHeight = 0;

    void _destroyGPUTextures();
#endif
};

class RenderGraph {
    struct PassEntry {
        std::unique_ptr<RenderPass> pass;
        GpuTimer timer;
        std::string name;
    };
    std::vector<PassEntry> _entries;

public:
    // Off by default — GL_TIMESTAMP queries add real overhead (driver ordering
    // barriers around pass boundaries) even though readback is async.
    // Enable only when the timing panel is in use.
    bool gpuProfilingEnabled = false;

    ~RenderGraph();

    void AddPass(std::unique_ptr<RenderPass> pass);
    void Render(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr);

    // Returns {passName, gpuMs} for every pass (one frame behind, stall-free).
    std::vector<std::pair<std::string, float>> GetTimings() const;

    template<typename T> T* GetPass() {
        for (auto& e : _entries) {
            if (auto* t = dynamic_cast<T*>(e.pass.get())) return t;
        }
        return nullptr;
    }
};

class Renderer {
public:
    enum class RenderPath { Forward, Deferred };

    struct RenderTargetProps {
        int width = INIT_FRAMEBUFFER_WIDTH;
        int height = INIT_FRAMEBUFFER_HEIGHT;
        int numSamples = MSAA_NUM_SAMPLES;
    };

    Renderer() = default;
    ~Renderer() = default;

    void Init(int width, int height);
    void Cleanup();
    void Resize(int width, int height);

    void CheckFramebufferStatus(const std::string& prefix);
    void CheckErrors(const std::string& prefix);

    void PushDebugLine(DebugVertex from, DebugVertex to);

    void EnableWireframe(bool enable = true) {
        wireframeEnabled = enable;
    }

    template<typename T> T* GetPass() {
        return _renderGraph ? _renderGraph->GetPass<T>() : nullptr;
    }

    std::vector<std::pair<std::string, float>> GetTimings() const {
        return _renderGraph ? _renderGraph->GetTimings() : std::vector<std::pair<std::string, float>>{};
    }

#ifdef AE_GPU_TIMER_ENABLED
    bool& GpuProfilingEnabled() {
        return _renderGraph->gpuProfilingEnabled;
    }
#endif

    void SetCapability(const GLenum& cap, bool enable = true) {
        if (enable) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    void SubmitCommand(const RenderCommand& cmd);
    void RenderFrame(GraphicsSubsystem* ctx, float dt);

    void BeginTransformFeedbackPass();
    void BindTransformFeedbackBuffer(GLuint bufferId, GLuint index = 0);
    void EndTransformFeedbackPass();

    auto& GetOpaqueQueue() {
        return _opaqueQueue;
    }
    auto& GetTransparentQueue() {
        return _transparentQueue;
    }

    void SubmitUICommand(const BatchDrawCommand& cmd);

    auto& GetUIQueue() {
        return _hudQueue;
    }

    void SubmitCanvasCommand(const BatchDrawCommand& cmd);
    auto& GetCanvasQueue() {
        return _canvasQueue;
    }

    // Read rendered pixels asynchronously. On this OpenGL backend the readback
    // is actually synchronous; the callback fires before this method returns.
    // Pixels are in RGBA order, top-to-bottom (flipped from the raw GL output).
    void readPixelsAsync(PixelReadbackCallback callback);

    // Two-phase async readback for high-throughput video recording.
    // Both must be called each frame from the render/GL thread.
    //
    // Phase 1 – issue this frame's DMA into the next PBO slot (non-blocking).
    void schedulePixelReadback();
    // Phase 2 – map the PBO written 2 frames ago and return its pixel data.
    // Returns nullopt while the pipeline is priming (first 2 frames) or if
    // no readback has been scheduled. GpuImageData.bottomUp is true; move
    // GpuImageData.data into the destination to avoid a second memcpy.
    std::optional<GpuImageData> collectPixelReadback();
    // Release PBO and FBO resources. Call when recording stops or on Cleanup.
    void destroyReadbackPBOs();

    glm::vec4 clearColor = glm::vec4(0.15f, 0.183f, 0.2f, 1.0f);
    bool wireframeEnabled = false;

    // Non-null only while PlanarReflectionPass drives the scene passes with a
    // mirrored camera; passes fall back to the main camera + sceneRT when null.
    const RenderViewOverride* viewOverride = nullptr;

    // Abstract render targets (backend-independent)
    std::unique_ptr<RenderTarget> sceneRT;
    std::unique_ptr<RenderTarget> msaaResolveRT;

    // Abstract buffers (backend-independent)
    std::unique_ptr<Buffer> debugBuffer;
    std::unique_ptr<Buffer> screenBuffer;

    // All OpenGL-specific state grouped here; WebGPU paths never touch these fields.
    struct GL {
        GLuint finalFBO = 0;
        std::array<GLuint, MAX_UNI_LIGHTS> uniShadowMaps = {};
        std::array<GLuint, MAX_OMNI_LIGHTS> omniShadowMaps = {};
        GLuint envMap = 0, irradianceMap = 0;
        struct GBuffer {
            GLuint id = 0;
            GLuint positionRT = 0;
            GLuint normalRT = 0;
            GLuint albedoRT = 0;
            GLuint materialRT = 0;
            GLuint depthRT = 0;
        } gBuffer;
        GLuint shadowFBO = 0;
        GLuint canvasVAO = 0, canvasVBO = 0;
        GLuint screenQuadVAO = 0;
        GLuint skyboxVAO = 0, skyboxVBO = 0;
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        GLuint glesResolvedDepthTex = 0;
        GLuint glesResolvedDepthFBO = 0;
        int glesResolvedDepthWidth = 0;
        int glesResolvedDepthHeight = 0;
#endif
    } gl;

    // Per-frame time (seconds) forwarded from RenderFrame for animated passes.
    float frameTime = 0.0f;

    // Returns the resolved (non-MSAA) depth texture for screen-space effects.
    GLuint GetResolvedDepthTexture() const {
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        // WebGL 2.0 does not allow reading from the depth texture of the bound FBO (feedback loop).
        // Since sceneRT is single-sampled on WebGL, we can read from sceneRT's depth texture instead!
        // But if sceneRT is multi-sampled (MSAA enabled), sceneRT has no depth texture (returns 0),
        // so we must read from the glesResolvedDepthTex where we resolved the depth.
        if (sceneRT && sceneRT->GetNumSamples() > 1) {
            return gl.glesResolvedDepthTex;
        }
        if (!sceneRT) return 0;
        return static_cast<GLuint>(sceneRT->GetDepthTextureID());
#else
        if (!msaaResolveRT) return 0;
        return static_cast<GLuint>(msaaResolveRT->GetDepthTextureID());
#endif
    }

    struct SortableCommand {
        RenderCommand cmd;
        uint64_t sortKey;
    };

private:
    std::vector<RenderCommand> _commandList;

    // Bucketed and sorted queues
    std::vector<SortableCommand> _opaqueQueue;// scene, voxel chunks, skybox
    std::vector<SortableCommand> _afterOpaqueQueue;// raymarching, GPU particles
    std::vector<SortableCommand> _transparentQueue;// particles, world UI
    std::vector<SortableCommand> _gizmoQueue;// world debug UI
    std::vector<BatchDrawCommand> _hudQueue;// HUD (RmlUi)
    std::vector<BatchDrawCommand> _canvasQueue;// immediate mode canvas (Lua)

    std::unique_ptr<RenderGraph> _renderGraph;
    RenderPath _currRenderPath = RenderPath::Forward;

    void CreateFBOs();
    void DestroyFBOs();
    void CreateRTs(const RenderTargetProps& props);
    void DestroyRTs();

    void CreateCanvasVAO();
    void CreateScreenBuffer();
    void CreateDebugBuffer();
    void CreateScreenQuadVAO();

    // Async readback state (3-PBO ring buffer + persistent FBO).
    static constexpr int READBACK_PBO_COUNT = 3;
    GLuint m_readbackPBOs[READBACK_PBO_COUNT] = {};
    GLuint m_readbackFBO = 0;
    uint32_t m_readbackFrameIdx = 0;
    uint32_t m_readbackPBOWidth = 0;
    uint32_t m_readbackPBOHeight = 0;

    void SortAndBucket(const glm::vec3& cameraPos);
    uint64_t CalculateSortKey(const RenderCommand& cmd, const glm::vec3& cameraPos);
    void BucketCommands(const glm::vec3& cameraPos);
    void SortOpaque();
    void SortTransparent();

    std::unique_ptr<BatchRenderer2D> m_BatchRenderer;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    std::unique_ptr<GPUCanvasPass> m_GPUCanvasPass;
#endif

public:
    BatchRenderer2D* GetBatchRenderer() const {
        return m_BatchRenderer.get();
    }

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    GPUCanvasPass* GetGPUCanvasPass() const {
        return m_GPUCanvasPass.get();
    }

    // Shadow-map handoff: written by ShadowPass each frame (non-owning view —
    // ShadowPass owns the texture), consumed by ForwardOpaquePass. The light
    // VP already includes the GL→WebGPU [-1,1]→[0,1] depth-range fixup, so
    // consumers use it as-is for textureSampleCompare.
    WGPUTextureView wgpuShadowMapView = nullptr;
    glm::mat4 wgpuShadowLightVP = glm::mat4(1.0f);
#endif
};

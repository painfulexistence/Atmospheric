#pragma once
#include "asset_manager.hpp"
#include "gpu_timer.hpp"
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "glm/mat4x4.hpp"
#include "globals.hpp"
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

class GraphicsServer;
class ShaderProgram;
class Renderer;

struct RenderCommand {
    MeshHandle mesh;
    // Material* material; // TODO: currently material is coupled with mesh
    glm::mat4 transform;
};

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#endif

class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) = 0;
};

class ShadowPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Renders MeshType::PRIM meshes from the opaque queue. TERRAIN (tessellation,
// GL-only) and VOXEL (handled by VoxelChunkPass) are skipped under WebGPU.
class ForwardOpaquePass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~ForwardOpaquePass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat);
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUBindGroup _getOrCreateTexBG(uint32_t texID);

    WGPUDevice          _gpuDevice       = nullptr;
    WGPUQueue           _gpuQueue        = nullptr;
    WGPURenderPipeline  _pipeline        = nullptr;
    WGPUBindGroupLayout _uniformBGL      = nullptr;
    WGPUBindGroupLayout _texBGL          = nullptr;
    WGPUBindGroup       _uniformBG       = nullptr;
    // Frame-constant data (viewProj, camera, light) — one fixed-size binding.
    WGPUBuffer          _frameUniformBuf = nullptr;
    // Per-draw data (model, surface params) — dynamic-offset slot per mesh,
    // mirrors VoxelChunkPass's pattern (see voxel_chunk_pass.cpp).
    WGPUBuffer          _drawUniformBuf  = nullptr;
    uint32_t            _drawSlotCapacity = 0;

    // AssetManager::GetDefaultTextures() is GL-only; this pass owns its own
    // fallback texture/sampler for meshes with no base map under WebGPU.
    WGPUTexture _whiteTex = nullptr;
    WGPUSampler _sampler  = nullptr;
    std::unordered_map<uint32_t, WGPUBindGroup> _texBGCache;
#endif
};

class DeferredGeometryPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class DeferredLightingPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// For particles, world UI
class TransparentPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class MSAAResolvePass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class WorldCanvasPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class CanvasPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Chromatic aberration is not in here -- it composites with tonemap via the
// caEnabled/caStrength uniforms on PostProcessPass instead of its own shader.
enum class PostEffect { None, CRT, VHS, ColorGrading, Posterize, Sobel, Edges, Vignette };

// Final composite blit: ACES tonemapping + optional chromatic aberration.
class PostProcessPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~PostProcessPass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool       tonemapEnabled = true;
    float      exposure       = 0.5f;

    bool  caEnabled  = false;
    float caStrength = 0.005f;

    PostEffect postEffect = PostEffect::None;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat swapchainFormat);
    WGPUDevice          _gpuDevice   = nullptr;
    WGPUQueue           _gpuQueue    = nullptr;
    WGPURenderPipeline  _pipeline    = nullptr;
    WGPUBindGroupLayout _uniformBGL  = nullptr;
    WGPUBindGroupLayout _texBGL      = nullptr;
    WGPUBindGroup       _uniformBG   = nullptr;
    WGPUBuffer          _uniformBuf  = nullptr;
    WGPUSampler         _sampler     = nullptr;
    // Texture bind group is recreated whenever sceneRT's texture changes
    // (e.g. on resize); cached here to avoid rebuilding it every frame.
    WGPUBindGroup       _texBG       = nullptr;
    WGPUTexture         _texBGSource = nullptr;
#endif
};

// TODO: rename this
class UIPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Flat billboard quad at the light source position — reads SunComponent for visual params.
class SunPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SunPass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat);
    WGPUDevice          _gpuDevice  = nullptr;
    WGPUQueue           _gpuQueue   = nullptr;
    WGPURenderPipeline  _pipeline   = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup       _uniformBG  = nullptr;
    WGPUBuffer          _uniformBuf = nullptr;
    WGPUBuffer          _vertexBuf  = nullptr;
#endif
};

// Gradient sky rendered at depth=1 (behind everything).  Matches VX's Skybox.
class SkyboxPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SkyboxPass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    glm::vec3 skyColor     = glm::vec3(0.686f, 0.933f, 0.933f); // VX COLOR_MINT_GREEN
    glm::vec3 horizonColor = glm::vec3(1.000f, 0.980f, 0.804f); // VX COLOR_LEMON_CREAM

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat);
    WGPUDevice          _gpuDevice  = nullptr;
    WGPUQueue           _gpuQueue   = nullptr;
    WGPURenderPipeline  _pipeline   = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup       _uniformBG  = nullptr;
    WGPUBuffer          _uniformBuf = nullptr;
    WGPUBuffer          _vertexBuf  = nullptr;
#endif
};

// Renders MeshType::VOXEL meshes from the opaque queue using the voxel shader.
class VoxelChunkPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~VoxelChunkPass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    int paletteIndex = 4;  // 0-5; 4 = VX Palette 5 (soft cool blue-grey)

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat);
    // Grows _drawUniformBuf/_uniformBG to fit at least drawCount dynamic-offset slots.
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUDevice          _gpuDevice       = nullptr;
    WGPUQueue           _gpuQueue        = nullptr;
    WGPURenderPipeline  _pipeline        = nullptr;
    WGPUBindGroupLayout _uniformBGL      = nullptr;
    WGPUBindGroup       _uniformBG       = nullptr;
    // Frame-constant data (viewProj, light, fog, camera) — one fixed-size binding.
    WGPUBuffer          _frameUniformBuf = nullptr;
    // Per-draw data (model, normalMat) — one slot per mesh, selected via dynamic offset.
    // Sized lazily; all slots are written in a single call before any draw is
    // recorded each frame, since wgpuQueueWriteBuffer ordering is relative to
    // Queue::Submit, not to render-pass recording order.
    WGPUBuffer          _drawUniformBuf  = nullptr;
    uint32_t            _drawSlotCapacity = 0;
#endif
};

// Renders MeshType::PRIM water meshes tagged via material renderQueue.
class WaterPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~WaterPass() override;
#endif
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    float time = 0.0f;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat);
    void _ensureDrawCapacity(uint32_t drawCount);

    WGPUDevice          _gpuDevice       = nullptr;
    WGPUQueue           _gpuQueue        = nullptr;
    WGPURenderPipeline  _pipeline        = nullptr;
    WGPUBindGroupLayout _uniformBGL      = nullptr;
    WGPUBindGroup       _uniformBG       = nullptr;
    // Frame-constant data (viewProj, camera, light, time) — one fixed-size binding.
    WGPUBuffer          _frameUniformBuf = nullptr;
    // Per-draw data (model, water shading params) — dynamic-offset slot per
    // mesh, mirrors ForwardOpaquePass/VoxelChunkPass's pattern.
    WGPUBuffer          _drawUniformBuf  = nullptr;
    uint32_t            _drawSlotCapacity = 0;
#endif
};

// Pyramid bloom: threshold → downsample → upsample → composite.
class BloomPass : public RenderPass {
public:
    ~BloomPass() override;
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool  enabled       = false;
    float threshold     = 0.8f;
    float bloomStrength = 0.04f;

private:
    static constexpr int MIP_LEVELS = 5;

    struct MipRT {
        GLuint fbo = 0;
        GLuint tex = 0;
        int    w   = 0;
        int    h   = 0;
    };

    MipRT _mips[MIP_LEVELS];
    GLuint _tempFBO = 0;
    GLuint _tempTex = 0;
    bool  _initialized = false;
    int   _lastW = 0, _lastH = 0;

    void InitMips(int w, int h);
    void DrawScreenQuad(GLuint screenVAO);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // Simplified WebGPU bloom: single half-res threshold + separable
    // horizontal/vertical Gaussian blur (ping-ponging _brightA/_brightB) +
    // additive composite — a deliberate simplification of GL's 5-level mip
    // pyramid (see header comment). Avoids reading sceneRT's own texture
    // while it's bound as the composite pass's render attachment by first
    // copying it into _snapshotTex (requires CopySrc on sceneRT's texture,
    // see gpu_render_target.cpp).
    void _initGPU(WGPUDevice device, WGPUQueue queue);
    void _resizeGPU(int width, int height);

    WGPUDevice  _gpuDevice = nullptr;
    WGPUQueue   _gpuQueue  = nullptr;
    WGPUSampler _sampler   = nullptr;

    WGPURenderPipeline _threshPipeline = nullptr;
    WGPURenderPipeline _blurPipeline   = nullptr;
    WGPURenderPipeline _compPipeline   = nullptr;

    WGPUBindGroupLayout _threshBGL = nullptr;
    WGPUBindGroupLayout _blurBGL   = nullptr;
    WGPUBindGroupLayout _compBGL   = nullptr;

    WGPUBuffer _threshUniformBuf = nullptr;
    WGPUBuffer _blurHUniformBuf  = nullptr; // direction+texelSize, fixed per resize
    WGPUBuffer _blurVUniformBuf  = nullptr;
    WGPUBuffer _compUniformBuf   = nullptr;

    // Half-resolution ping-pong textures for the blur chain.
    WGPUTexture     _brightA = nullptr, _brightB = nullptr;
    WGPUTextureView _brightAView = nullptr, _brightBView = nullptr;
    // Full-resolution copy of sceneRT, refreshed each frame before composite.
    WGPUTexture     _snapshotTex  = nullptr;
    WGPUTextureView _snapshotView = nullptr;

    WGPUBindGroup _threshBG = nullptr;
    WGPUBindGroup _blurHBG  = nullptr; // reads _brightA, writes _brightB
    WGPUBindGroup _blurVBG  = nullptr; // reads _brightB, writes _brightA
    WGPUBindGroup _compBG   = nullptr; // reads _snapshotTex + _brightB(final)

    int _gpuWidth = 0, _gpuHeight = 0, _gpuHalfW = 0, _gpuHalfH = 0;

    void _destroyGPUTextures();
#endif
};

class RenderGraph {
    struct PassEntry {
        std::unique_ptr<RenderPass> pass;
        GpuTimer                    timer;
        std::string                 name;
    };
    std::vector<PassEntry> _entries;

public:
    // Off by default — GL_TIMESTAMP queries add real overhead (driver ordering
    // barriers around pass boundaries) even though readback is async.
    // Enable only when the timing panel is in use.
    bool gpuProfilingEnabled = false;

    ~RenderGraph();

    void AddPass(std::unique_ptr<RenderPass> pass);
    void Render(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr);

    // Returns {passName, gpuMs} for every pass (one frame behind, stall-free).
    std::vector<std::pair<std::string, float>> GetTimings() const;

    template<typename T>
    T* GetPass() {
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

    template<typename T>
    T* GetPass() { return _renderGraph ? _renderGraph->GetPass<T>() : nullptr; }

    std::vector<std::pair<std::string, float>> GetTimings() const {
        return _renderGraph ? _renderGraph->GetTimings() : std::vector<std::pair<std::string, float>>{};
    }

#ifdef AE_GPU_TIMER_ENABLED
    bool& GpuProfilingEnabled() { return _renderGraph->gpuProfilingEnabled; }
#endif

    void SetCapability(const GLenum& cap, bool enable = true) {
        if (enable) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    void SubmitCommand(const RenderCommand& cmd);
    void RenderFrame(GraphicsServer* ctx, float dt);

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

    // Abstract render targets (backend-independent)
    std::unique_ptr<RenderTarget> sceneRT;
    std::unique_ptr<RenderTarget> msaaResolveRT;

    // Abstract buffers (backend-independent)
    std::unique_ptr<Buffer> debugBuffer;
    std::unique_ptr<Buffer> screenBuffer;

    // All OpenGL-specific state grouped here; WebGPU paths never touch these fields.
    struct GL {
        GLuint finalFBO = 0;
        std::array<GLuint, MAX_UNI_LIGHTS>  uniShadowMaps = {};
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
        int    glesResolvedDepthWidth = 0;
        int    glesResolvedDepthHeight = 0;
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
    std::vector<SortableCommand> _opaqueQueue;      // scene, voxel chunks, skybox
    std::vector<SortableCommand> _afterOpaqueQueue; // raymarching, GPU particles
    std::vector<SortableCommand> _transparentQueue; // particles, world UI
    std::vector<SortableCommand> _gizmoQueue;       // world debug UI
    std::vector<BatchDrawCommand> _hudQueue;        // HUD (RmlUi)
    std::vector<BatchDrawCommand> _canvasQueue;     // immediate mode canvas (Lua)

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
    GPUCanvasPass* GetGPUCanvasPass() const { return m_GPUCanvasPass.get(); }
#endif
};

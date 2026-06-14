#pragma once
#include "asset_manager.hpp"
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "glm/mat4x4.hpp"
#include "globals.hpp"
#include "mesh.hpp"
#include "render_target.hpp"
#include <memory>

class GraphicsServer;
class ShaderProgram;
class Renderer;

struct RenderCommand {
    Mesh* mesh;
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

class ForwardOpaquePass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
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

// Final composite blit: ACES tonemapping + optional chromatic aberration.
class PostProcessPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool  tonemapEnabled = true;
    float exposure       = 0.5f;

    bool  caEnabled  = false;
    float caStrength = 0.005f;
};

// TODO: rename this
class UIPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Flat billboard quad at the light source position — reads SunComponent for visual params.
class SunPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Gradient sky rendered at depth=1 (behind everything).  Matches VX's Skybox.
class SkyboxPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    glm::vec3 skyColor     = glm::vec3(0.686f, 0.933f, 0.933f); // VX COLOR_MINT_GREEN
    glm::vec3 horizonColor = glm::vec3(1.000f, 0.980f, 0.804f); // VX COLOR_LEMON_CREAM
};

// Renders MeshType::VOXEL meshes from the opaque queue using the voxel shader.
class VoxelChunkPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Renders MeshType::PRIM water meshes tagged via material renderQueue.
class WaterPass : public RenderPass {
public:
    void Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    float time = 0.0f;
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
};

class RenderGraph {
    std::vector<std::unique_ptr<RenderPass>> _passes;

public:
    void AddPass(std::unique_ptr<RenderPass> pass);
    void Render(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc = nullptr);

    template<typename T>
    T* GetPass() {
        for (auto& p : _passes) {
            if (auto* t = dynamic_cast<T*>(p.get())) return t;
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

    glm::vec4 clearColor = glm::vec4(0.15f, 0.183f, 0.2f, 1.0f);
    bool wireframeEnabled = false;

    // Abstract render targets (backend-independent)
    std::unique_ptr<RenderTarget> sceneRT;
    std::unique_ptr<RenderTarget> msaaResolveRT;
    GLuint finalFBO = 0;  // default framebuffer (always 0)

    // Abstract buffers (backend-independent)
    std::unique_ptr<Buffer> debugBuffer;
    std::unique_ptr<Buffer> screenBuffer;

    // GL-specific: shadow maps
    std::array<GLuint, MAX_UNI_LIGHTS>  uniShadowMaps;
    std::array<GLuint, MAX_OMNI_LIGHTS> omniShadowMaps;
    GLuint envMap, irradianceMap;

    // GL-specific: GBuffer
    struct GBuffer {
        GLuint id;
        GLuint positionRT;
        GLuint normalRT;
        GLuint albedoRT;
        GLuint materialRT;
        GLuint depthRT;
    } gBuffer;

    GLuint shadowFBO;
    GLuint canvasVAO, canvasVBO;  // GL-specific: legacy canvas geometry

    // Screen-quad VAO shared by post-process passes (bloom, composite, etc.)
    GLuint screenQuadVAO = 0;
    GLuint skyboxVAO     = 0;
    GLuint skyboxVBO     = 0;

    // Per-frame time (seconds) forwarded from RenderFrame for animated passes.
    float frameTime = 0.0f;

    // Returns the resolved (non-MSAA) depth texture for screen-space effects.
    GLuint GetResolvedDepthTexture() const {
#ifdef __EMSCRIPTEN__
        // WebGL 2.0 does not allow reading from the depth texture of the bound FBO (feedback loop).
        // Since sceneRT is single-sampled on WebGL, we can read from sceneRT's depth texture instead!
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

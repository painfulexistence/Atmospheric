#include "renderer.hpp"
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include "asset_manager.hpp"
#include "batch_renderer_2d.hpp"
#include "canvas_drawable.hpp"
#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "gfx_factory.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "graphics_subsystem.hpp"
#include "particle_subsystem.hpp"
#include "physics_subsystem_2d.hpp"
#include "window.hpp"
#include <algorithm>
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#include <webgpu/webgpu.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
#include <SDL3/SDL.h>
#endif
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// Define AE_GL_DEBUG_PROBES at build time to enable fine-grained GL error
// probes inside render passes. Used to bisect sticky INVALID_OPERATION etc.
// on GLES3 ports. Off by default to avoid the per-call glGetError cost.
#ifdef AE_GL_DEBUG_PROBES
#define AE_GL_PROBE(target, name) (target).CheckErrors(name)
#else
#define AE_GL_PROBE(target, name) ((void)0)
#endif

static GLenum GetGLPrimitiveType(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::Triangles:     return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::Lines:          return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::Points:        return GL_POINTS;
    }
    return GL_TRIANGLES;
}

struct RenderBatch {
    MeshHandle mesh;
    std::vector<InstanceData> instances;
};

static std::vector<RenderBatch> BuildBatches(const std::vector<Renderer::SortableCommand>& queue) {
    std::vector<RenderBatch> batches;
    RenderBatch currentBatch;
    currentBatch.mesh = queue[0].cmd.mesh;

    for (const auto& sortable : queue) {
        const auto& cmd = sortable.cmd;

        if (currentBatch.mesh != cmd.mesh) {
            if (currentBatch.mesh.IsValid()) {
                batches.push_back(std::move(currentBatch));
            }
            currentBatch.instances.clear();
            currentBatch.mesh = cmd.mesh;
        }
        InstanceData instance;
        instance.modelMatrix = cmd.transform;
        currentBatch.instances.push_back(instance);
    }

    if (!currentBatch.instances.empty()) {
        batches.push_back(std::move(currentBatch));
    }

    return batches;
}

static constexpr int MAX_CANVAS_TEXTURES = 32;

// Strips the leading digit-length prefix (GCC mangled names) and
// "class "/"struct " prefix (MSVC) from a typeid name string.
static std::string CleanTypeName(const char* raw) {
    while (*raw && std::isdigit(static_cast<unsigned char>(*raw))) ++raw;
    for (const char* prefix : {"class ", "struct "}) {
        if (std::strncmp(raw, prefix, std::strlen(prefix)) == 0) {
            raw += std::strlen(prefix);
            break;
        }
    }
    return raw;
}

RenderGraph::~RenderGraph() {
    for (auto& e : _entries) e.timer.Destroy();
}

void RenderGraph::AddPass(std::unique_ptr<RenderPass> pass) {
    PassEntry e;
    e.name = CleanTypeName(typeid(*pass).name());
    e.timer.Init();
    e.pass = std::move(pass);
    _entries.push_back(std::move(e));
}

void RenderGraph::Render(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    for (auto& e : _entries) {
#ifdef AE_GPU_TIMER_ENABLED
        if (gpuProfilingEnabled) e.timer.Begin();
#endif
        e.pass->Execute(ctx, renderer, enc);
#ifdef AE_GPU_TIMER_ENABLED
        if (gpuProfilingEnabled) e.timer.End();
#endif
    }
}

std::vector<std::pair<std::string, float>> RenderGraph::GetTimings() const {
    std::vector<std::pair<std::string, float>> out;
    out.reserve(_entries.size() + 1);
    float total = 0.0f;
    for (auto& e : _entries) {
        out.push_back({e.name, e.timer.GetMs()});
        total += e.timer.GetMs();
    }
    out.push_back({"[Total]", total});
    return out;
}

void Renderer::Init(int width, int height) {
    CreateFBOs();
    CreateRTs(RenderTargetProps{ width, height });
    CreateDebugBuffer();
    CreateCanvasVAO();
    CreateScreenBuffer();

    m_BatchRenderer = std::make_unique<BatchRenderer2D>();
    m_BatchRenderer->Init();

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    m_GPUCanvasPass = std::make_unique<GPUCanvasPass>();
#endif

    // Screen-space quad VAO for post-process passes (bloom, etc.)
    {
        static const float quadVerts[] = {
            -1.f, -1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 1.f,
        };
        GLuint vbo;
        glGenVertexArrays(1, &screenQuadVAO);
        glGenBuffers(1, &vbo);
        glBindVertexArray(screenQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    // ── Skybox cube VAO ───────────────────────────────────────────────
    {
        static const float cubeVerts[] = {
            -1, -1, -1,  1, -1, -1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1, -1, -1,
            -1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1,  1,
            -1,  1,  1, -1,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1, -1,  1,  1,
             1,  1,  1,  1,  1, -1,  1, -1, -1,  1, -1, -1,  1, -1,  1,  1,  1,  1,
            -1, -1, -1, -1, -1,  1,  1, -1,  1,  1, -1,  1,  1, -1, -1, -1, -1, -1,
            -1,  1, -1, -1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, -1, -1,  1, -1,
        };
        glGenVertexArrays(1, &skyboxVAO);
        glGenBuffers(1, &skyboxVBO);
        glBindVertexArray(skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    _renderGraph = std::make_unique<RenderGraph>();
    _renderGraph->AddPass(std::make_unique<ShadowPass>());
    _renderGraph->AddPass(std::make_unique<ForwardOpaquePass>());
    _renderGraph->AddPass(std::make_unique<SkyboxPass>());   // after clear, fills empty sky pixels
    _renderGraph->AddPass(std::make_unique<SunPass>());
    _renderGraph->AddPass(std::make_unique<VoxelChunkPass>());
    _renderGraph->AddPass(std::make_unique<MSAAResolvePass>());
    _renderGraph->AddPass(std::make_unique<WaterPass>());
    _renderGraph->AddPass(std::make_unique<WorldCanvasPass>());// World sprites with depth testing
    _renderGraph->AddPass(std::make_unique<CanvasPass>());// 2D sprites, world space ortho, with no depth testing
    _renderGraph->AddPass(std::make_unique<BloomPass>());
    _renderGraph->AddPass(std::make_unique<PostProcessPass>());
    _renderGraph->AddPass(std::make_unique<UIPass>());
}

void Renderer::Cleanup() {
    if (m_BatchRenderer) {
        m_BatchRenderer->Shutdown();
        m_BatchRenderer.reset();
    }
    DestroyRTs();
    DestroyFBOs();
    destroyReadbackPBOs();

    // debug and screen are now std::unique_ptr<Buffer> that auto-destruct
    glDeleteVertexArrays(1, &canvasVAO);
    glDeleteBuffers(1, &canvasVBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
}

void Renderer::Resize(int width, int height) {
    DestroyRTs();
    CreateRTs(RenderTargetProps{ width, height });
    // PBOs are sized to the RT dimensions; invalidate so they're rebuilt next schedule.
    destroyReadbackPBOs();
}


void Renderer::SubmitCommand(const RenderCommand& cmd) {
    _commandList.push_back(cmd);
}

void Renderer::BeginTransformFeedbackPass() {
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);
}

void Renderer::BindTransformFeedbackBuffer(GLuint bufferId, GLuint index) {
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, index, bufferId);
}

void Renderer::EndTransformFeedbackPass() {
    glEndTransformFeedback();
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);// Unbind
    glDisable(GL_RASTERIZER_DISCARD);
}

void Renderer::SortAndBucket(const glm::vec3& cameraPos) {
    ZoneScopedN("Renderer::SortAndBucket");
    // Clear previous frame's sorted queues
    _opaqueQueue.clear();
    _transparentQueue.clear();
    // _hudQueue.clear(); // Managed separately
    _gizmoQueue.clear();
    _afterOpaqueQueue.clear();

    // Bucket commands based on material properties
    BucketCommands(cameraPos);

    // Sort each queue appropriately
    SortOpaque();
    SortTransparent();

    // Clear command buffer
    _commandList.clear();
}

// Resolves a mesh's material handle; draws that lost their material (e.g.
// after a scene unload) fall back to default-constructed material state
// instead of dereferencing a dangling pointer.
static Material* ResolveMaterialOrFallback(MaterialHandle handle) {
    if (Material* mat = AssetManager::Get().ResolveMaterial(handle)) {
        return mat;
    }
    static Material fallback{ MaterialProps{} };
    return &fallback;
}

uint64_t Renderer::CalculateSortKey(const RenderCommand& cmd, const glm::vec3& cameraPos) {
    Mesh* meshPtr = AssetManager::Get().GetMeshPtr(cmd.mesh);
    Material* mat = meshPtr ? AssetManager::Get().ResolveMaterial(meshPtr->GetMaterial()) : nullptr;
    if (!mat) return 0;

    // Calculate depth (distance from camera)
    glm::vec3 objPos = glm::vec3(cmd.transform[3]);
    float depth = glm::length(objPos - cameraPos);

    // Get render queue
    int renderQueue = mat->GetFinalRenderQueue();

    // Get material and mesh IDs for batching; handle IDs are stable across
    // runs, unlike the pointer bits used before.
    uint32_t materialID = meshPtr->GetMaterial().id & 0xFFFF;
    uint32_t meshID = cmd.mesh.id & 0xFFFF;

    // Generate 64-bit sort key
    // [16 bits: render queue] [16 bits: depth] [16 bits: material] [16 bits: mesh]
    uint64_t key = 0;
    key |= static_cast<uint64_t>(renderQueue & 0xFFFF) << 48;
    key |= static_cast<uint64_t>(static_cast<uint16_t>(depth * 100.0f) & 0xFFFF) << 32;
    key |= static_cast<uint64_t>(materialID & 0xFFFF) << 16;
    key |= static_cast<uint64_t>(meshID & 0xFFFF);

    return key;
}

void Renderer::SortOpaque() {
    // Front-to-back sorting: render near objects first to reduce overdraw
    std::sort(_opaqueQueue.begin(), _opaqueQueue.end(), [](const SortableCommand& a, const SortableCommand& b) {
        return a.sortKey < b.sortKey;
    });
}

void Renderer::SortTransparent() {
    // Back-to-front sorting: render far objects first for correct blending
    std::sort(
      _transparentQueue.begin(),
      _transparentQueue.end(),
      [](const SortableCommand& a, const SortableCommand& b) { return a.sortKey > b.sortKey; }
    );
}

void Renderer::BucketCommands(const glm::vec3& cameraPos) {
    for (const auto& cmd : _commandList) {
        Mesh* meshPtr = AssetManager::Get().GetMeshPtr(cmd.mesh);
        Material* mat = meshPtr ? AssetManager::Get().ResolveMaterial(meshPtr->GetMaterial()) : nullptr;
        if (!mat) continue;

        int queue = mat->GetFinalRenderQueue();
        uint64_t sortKey = CalculateSortKey(cmd, cameraPos);
        SortableCommand sortable{ cmd, sortKey };

        // Bucket based on render queue
        if (queue < static_cast<int>(RenderQueue::Transparent)) {
            _opaqueQueue.push_back(sortable);
        } else if (queue < static_cast<int>(RenderQueue::Overlay)) {
            _transparentQueue.push_back(sortable);
        } else {
            // _hudQueue.push_back(sortable); // _hudQueue is now for RmlUi
            // TODO: Handle overlay objects
        }
    }
}

// ... SortOpaque, SortTransparent ...

void Renderer::RenderFrame(GraphicsSubsystem* ctx, float dt) {
    ZoneScopedN("Renderer::RenderFrame");
    AE_GL_PROBE(*this, "RenderFrame: entry");
#if defined(__APPLE__) && TARGET_OS_IOS
    // SDL3 iOS GLES view uses its own FBO (not 0). Query each frame.
    {
        SDL_Window* win = static_cast<SDL_Window*>(Window::Get()->GetNativeHandle());
        SDL_PropertiesID props = SDL_GetWindowProperties(win);
        Sint64 fb = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
        finalFBO = static_cast<GLuint>(fb);
    }
    AE_GL_PROBE(*this, "RenderFrame: after query iOS FBO");
#endif
    frameTime += dt;
    SortAndBucket(ctx->GetMainCamera()->GetEyePosition());
    _renderGraph->Render(ctx, *this);

    _hudQueue.clear();
    _canvasQueue.clear();
}

void Renderer::CheckFramebufferStatus(const std::string& prefix) {
    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_UNDEFINED", prefix));
        case GL_FRAMEBUFFER_UNSUPPORTED:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_UNSUPPORTED", prefix));
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT", prefix));
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT", prefix));
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE", prefix));
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER", prefix));
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER", prefix));
        case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            throw std::runtime_error(fmt::format("{}: GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS", prefix));
#endif
        default:
            throw std::runtime_error(fmt::format("{}: Unknown error code {}", prefix, status));
        }
    }
}

void Renderer::CheckErrors(const std::string& prefix) {
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
        std::string error;
        switch (errorCode) {
        // Reference: https://learnopengl.com/In-Practice/Debugging
        case GL_INVALID_ENUM:
            error = "INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            error = "INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            error = "INVALID_OPERATION";
            break;
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
        case GL_STACK_OVERFLOW:
            error = "STACK_OVERFLOW";
            break;
        case GL_STACK_UNDERFLOW:
            error = "STACK_UNDERFLOW";
            break;
#endif
        case GL_OUT_OF_MEMORY:
            error = "OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            error = "INVALID_FRAMEBUFFER_OPERATION";
            break;
        default:
            error = "UNKNOWN";
            break;
        }
        ConsoleSubsystem::Get()->Error(fmt::format("{}: {}\n", prefix, error));
    }
}

void Renderer::CreateFBOs() {
    glGenFramebuffers(1, &shadowFBO);
    glGenFramebuffers(1, &gBuffer.id);
}

void Renderer::DestroyFBOs() {
    glDeleteFramebuffers(1, &shadowFBO);
    glDeleteFramebuffers(1, &gBuffer.id);
}

void Renderer::CreateRTs(const RenderTargetProps& props) {
    // 1. Create and set shadow pass attachments
    for (int i = 0; i < MAX_UNI_LIGHTS; ++i) {
        GLuint map;
        glGenTextures(1, &map);
        glBindTexture(GL_TEXTURE_2D, map);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        uniShadowMaps[i] = map;
    }
    for (int i = 0; i < MAX_OMNI_LIGHTS; ++i) {
        GLuint map;
        glGenTextures(1, &map);
        glBindTexture(GL_TEXTURE_CUBE_MAP, map);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        for (int f = 0; f < 6; ++f) {
            glTexImage2D(
              GL_TEXTURE_CUBE_MAP_POSITIVE_X + f,
              0,
              GL_DEPTH_COMPONENT32F,
              SHADOW_W,
              SHADOW_H,
              0,
              GL_DEPTH_COMPONENT,
              GL_FLOAT,
              nullptr
            );
        }
        omniShadowMaps[i] = map;
    }

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#else
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    for (int i = 0; i < static_cast<int>(uniShadowMaps.size()); ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, uniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    for (int i = 0; i < static_cast<int>(omniShadowMaps.size()); ++i) {
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, omniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
#endif
    CheckErrors("Create shadow RTs");

    // 2. Scene render target (MSAA on forward path; RBO-based on WebGL, texture-based on desktop)
    {
        RenderTarget::Props p;
        p.width = props.width;
        p.height = props.height;
        p.withDepth = true;
        p.hdr = true;
        if (_currRenderPath == RenderPath::Forward)
            p.numSamples = props.numSamples;
        sceneRT = GfxFactory::CreateRenderTarget(p);
    }

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (sceneRT && sceneRT->GetNumSamples() > 1) {
        glGenTextures(1, &glesResolvedDepthTex);
        glBindTexture(GL_TEXTURE_2D, glesResolvedDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &glesResolvedDepthFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, glesResolvedDepthFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, glesResolvedDepthTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ConsoleSubsystem::Get()->Error("Renderer: WebGL resolved depth FBO incomplete!");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);

        glesResolvedDepthWidth = props.width;
        glesResolvedDepthHeight = props.height;
    }
#endif

    // 3. MSAA resolve render target (non-MSAA, for post-process input)
    {
        RenderTarget::Props p;
        p.width = props.width;
        p.height = props.height;
        p.withDepth = true;
        p.hdr = true;
        p.filtered = true;
        msaaResolveRT = GfxFactory::CreateRenderTarget(p);
    }

    // 4. Create and set geometry pass attachments
    glGenTextures(1, &gBuffer.positionRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.positionRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.normalRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.normalRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.albedoRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.albedoRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.materialRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.materialRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.depthRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.depthRT);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
    );

    glGenFramebuffers(1, &gBuffer.id);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer.id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gBuffer.positionRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gBuffer.normalRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gBuffer.albedoRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gBuffer.materialRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gBuffer.depthRT, 0);
    std::array<GLuint, 4> attachments = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(attachments.size(), attachments.data());
    CheckFramebufferStatus("G-buffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
    CheckErrors("Create G-buffer RTs");
}

void Renderer::DestroyRTs() {
    sceneRT.reset();
    msaaResolveRT.reset();

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (glesResolvedDepthFBO) {
        glDeleteFramebuffers(1, &glesResolvedDepthFBO);
        glesResolvedDepthFBO = 0;
    }
    if (glesResolvedDepthTex) {
        glDeleteTextures(1, &glesResolvedDepthTex);
        glesResolvedDepthTex = 0;
    }
    glesResolvedDepthWidth = 0;
    glesResolvedDepthHeight = 0;
#endif

    glDeleteTextures(1, &gBuffer.positionRT);
    glDeleteTextures(1, &gBuffer.normalRT);
    glDeleteTextures(1, &gBuffer.albedoRT);
    glDeleteTextures(1, &gBuffer.materialRT);
    glDeleteTextures(1, &gBuffer.depthRT);
}

void Renderer::CreateCanvasVAO() {
    glGenVertexArrays(1, &canvasVAO);
    glGenBuffers(1, &canvasVBO);

    glBindVertexArray(canvasVAO);
    glBindBuffer(GL_ARRAY_BUFFER, canvasVBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, texCoord));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, color));
    glVertexAttribIPointer(3, 1, GL_INT, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, texIndex));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

void Renderer::CreateScreenBuffer() {
    std::array<ScreenVertex, 4> verts = { {
        { { -1.0f,  1.0f }, { 0.0f, 1.0f } },
        { { -1.0f, -1.0f }, { 0.0f, 0.0f } },
        { {  1.0f,  1.0f }, { 1.0f, 1.0f } },
        { {  1.0f, -1.0f }, { 1.0f, 0.0f } },
    } };
    screenBuffer = GfxFactory::CreateBuffer();
    screenBuffer->Initialize(VertexFormat::Screen, BufferUsage::Static);
    screenBuffer->Upload(verts.data(), verts.size(), sizeof(ScreenVertex));
}

void Renderer::CreateDebugBuffer() {
    debugBuffer = GfxFactory::CreateBuffer();
    debugBuffer->Initialize(VertexFormat::Debug, BufferUsage::Stream);
}

void ShadowPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("ShadowPass");
    AE_GL_PROBE(renderer, "Shadow pass: entry");
    glViewport(0, 0, SHADOW_W, SHADOW_H);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    // Combine queues for shadow casting
    // TODO: Filter out objects that don't cast shadows
    std::vector<Renderer::SortableCommand> shadowCasters;
    for (auto& cmd : renderer.GetOpaqueQueue())
        shadowCasters.push_back(cmd);
    for (auto& cmd : renderer.GetTransparentQueue())
        shadowCasters.push_back(cmd);

    // Batching for shadow casters
    std::vector<RenderBatch> batches;
    if (!shadowCasters.empty()) {
        batches = BuildBatches(shadowCasters);
    }

    // 1. Render shadow map for directional light
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.shadowFBO);

    auto mainLight = ctx->GetMainLight();

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderer.uniShadowMaps[0], 0);
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#endif
    glClear(GL_DEPTH_BUFFER_BIT);
    auto depthShader = ctx->GetShader("depth");
    depthShader->Activate();
    depthShader->SetUniform(
      std::string("ProjectionView"), mainLight->GetProjectionMatrix(0) * mainLight->GetViewMatrix()
    );

    for (const auto& batch : batches) {
        Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
        const auto& instances = batch.instances;// NOTES: no need to check for empty instances here, as we only add
                                                // batches with instances (and OpenGL will handle 0 instances whatever)

        if (!mesh || !mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

        Material* material = ResolveMaterialOrFallback(mesh->GetMaterial());

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (material->cullFaceEnabled)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);

        if (mesh->type == MeshType::PRIM) {
            glBindVertexArray(mesh->vao);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
            // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
            for (const auto& inst : instances) {
                depthShader->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                  GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
            }
#else
            // Upload ALL instances for this batch once
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW);

            // Instanced Draw
            glDrawElementsInstanced(
              GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
            );
#endif
        }
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.finalFBO);

    // 2. Render shadow maps for point lights
    if (ctx->pointLights.size() == 0) {
        return;
    }

    auto depthCubemapShader = ctx->GetShader("depth_cubemap");
    depthCubemapShader->Activate();
    int auxShadows = 0;
    for (int i = 0; i < ctx->pointLights.size(); ++i) {
        LightComponent* l = ctx->pointLights[i];
        if (!l->castShadow) continue;
        if (auxShadows++ >= MAX_OMNI_LIGHTS) break;

        for (int f = 0; f < 6; ++f) {
            GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + f;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, face, renderer.omniShadowMaps[i], 0);
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
            GLenum drawBuffers[] = { GL_NONE };
            glDrawBuffers(1, drawBuffers);
#endif
            glClear(GL_DEPTH_BUFFER_BIT);
            depthCubemapShader->SetUniform(std::string("LightPosition"), l->GetPosition());
            depthCubemapShader->SetUniform(
              std::string("ProjectionView"), l->GetProjectionMatrix(0) * l->GetViewMatrix(face)
            );

            for (const auto& batch : batches) {
                Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
                const auto& instances = batch.instances;

                if (!mesh || !mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

                Material* material = ResolveMaterialOrFallback(mesh->GetMaterial());

                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);

                if (material->cullFaceEnabled)
                    glEnable(GL_CULL_FACE);
                else
                    glDisable(GL_CULL_FACE);

                if (mesh->type == MeshType::PRIM) {
                    glBindVertexArray(mesh->vao);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
                    // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
                    for (const auto& inst : instances) {
                        depthCubemapShader->SetUniform(std::string("World"), inst.modelMatrix);
                        glDrawElements(
                          GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                        );
                    }
#else
                    // Upload ALL instances for this batch once
                    glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
                    glBufferData(
                      GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                    );

                    // Instanced Draw
                    glDrawElementsInstanced(
                      GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                    );
#endif
                }
                glBindVertexArray(0);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.finalFBO);
}

void ForwardOpaquePass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("ForwardOpaquePass");
    AE_GL_PROBE(renderer, "Opaque pass: entry");
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
    AE_GL_PROBE(renderer, "Opaque pass: after bind sceneRT");
    // Bind textures
    auto& assetManager = AssetManager::Get();
    for (int i = 0; i < MAX_UNI_LIGHTS; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, renderer.uniShadowMaps[i]);
    }
    for (int i = 0; i < MAX_OMNI_LIGHTS; ++i) {
        glActiveTexture(GL_TEXTURE0 + UNI_SHADOW_MAP_COUNT + i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, renderer.omniShadowMaps[i]);
    }
    AE_GL_PROBE(renderer, "Opaque pass: after bind shadow maps");
    // Global static binding removed; textures are now dynamically bound per draw call

    auto mainLight = ctx->GetMainLight();
    glm::vec3 eyePos = ctx->GetMainCamera()->GetEyePosition();
    glm::mat4 projectionView = ctx->GetMainCamera()->GetProjectionMatrix() * ctx->GetMainCamera()->GetViewMatrix();

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    AE_GL_PROBE(renderer, "Opaque pass: after clear");

    auto terrainShader = ctx->GetShader("terrain");
    auto colorShader = ctx->GetShader("color");
    // 1. Batching Phase
    std::vector<RenderBatch> batches;

    if (!renderer.GetOpaqueQueue().empty()) {
        batches = BuildBatches(renderer.GetOpaqueQueue());
    }

    // 2. Drawing Phase
    for (const auto& batch : batches) {
        Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
        const auto& instances = batch.instances;

        if (!mesh || !mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

        Material* material = ResolveMaterialOrFallback(mesh->GetMaterial());

        AE_GL_PROBE(renderer, fmt::format("Opaque pass: batch entry (type={})", static_cast<int>(mesh->type)));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
        if (renderer.wireframeEnabled || material->polygonMode == PolygonMode::Line)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

        if (material->cullFaceEnabled)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);

        switch (mesh->type) {

        case MeshType::TERRAIN: {
            terrainShader->Activate();
            terrainShader->SetUniform(std::string("cam_pos"), eyePos);
            terrainShader->SetUniform(std::string("main_light.direction"), mainLight->direction);
            terrainShader->SetUniform(std::string("main_light.ambient"), mainLight->ambient);
            terrainShader->SetUniform(std::string("main_light.diffuse"), mainLight->diffuse);
            terrainShader->SetUniform(std::string("main_light.specular"), mainLight->specular);
            terrainShader->SetUniform(std::string("main_light.intensity"), mainLight->intensity);
            terrainShader->SetUniform(std::string("main_light.cast_shadow"), mainLight->castShadow ? 1 : 0);
            terrainShader->SetUniform(std::string("main_light.ProjectionView"), mainLight->GetProjectionViewMatrix(0));

            terrainShader->SetUniform(std::string("surf_params.diffuse"), material->diffuse);
            terrainShader->SetUniform(std::string("surf_params.specular"), material->specular);
            terrainShader->SetUniform(std::string("surf_params.ambient"), material->ambient);
            terrainShader->SetUniform(std::string("surf_params.shininess"), material->shininess);

            auto* tm = dynamic_cast<TerrainMaterial*>(material);
            terrainShader->SetUniform(std::string("tessellation_factor"), tm ? tm->tessellationFactor : 16.0f);
            terrainShader->SetUniform(std::string("height_scale"),         tm ? tm->heightScale        : 32.0f);
            glActiveTexture(GL_TEXTURE7);
            TextureHandle heightMap = material->heightMap;
            if (heightMap.IsValid() && static_cast<uint32_t>(heightMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(heightMap));
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            terrainShader->SetUniform(std::string("height_map_unit"), 7);
            terrainShader->SetUniform(std::string("ProjectionView"), projectionView);

            // Terrain is usually a single instance, but we handle it in the batch loop
            // Assuming terrain is not instanced for now or handled as single instance
            terrainShader->SetUniform(std::string("World"), instances[0].modelMatrix);

            glBindVertexArray(mesh->vao);
            AE_GL_PROBE(renderer, "Opaque pass: TERRAIN after bind VAO");
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
            glDrawArrays(GL_PATCHES, 0, mesh->vertCount);
#else
            glDrawArrays(GL_TRIANGLES, 0, mesh->vertCount);
#endif
            AE_GL_PROBE(renderer, "Opaque pass: TERRAIN after draw");
            glBindVertexArray(0);
            break;
        }

        case MeshType::SKY:
            // TODO: implement skybox rendering
            break;

        case MeshType::VOXEL:
            // Handled by VoxelChunkPass before this pass.
            break;

        case MeshType::PRIM:
        default:
            colorShader->Activate();
            colorShader->SetUniform(std::string("cam_pos"), eyePos);
            colorShader->SetUniform(std::string("time"), 0);
            colorShader->SetUniform(std::string("main_light.direction"), mainLight->direction);
            colorShader->SetUniform(std::string("main_light.ambient"), mainLight->ambient);
            colorShader->SetUniform(std::string("main_light.diffuse"), mainLight->diffuse);
            colorShader->SetUniform(std::string("main_light.specular"), mainLight->specular);
            colorShader->SetUniform(std::string("main_light.intensity"), mainLight->intensity);
            colorShader->SetUniform(std::string("main_light.cast_shadow"), mainLight->castShadow ? 1 : 0);
            colorShader->SetUniform(std::string("main_light.ProjectionView"), mainLight->GetProjectionViewMatrix(0));
            for (int i = 0; i < ctx->pointLights.size(); ++i) {
                LightComponent* l = ctx->pointLights[i];
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].position"), l->GetPosition()
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].ambient"), l->ambient
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].diffuse"), l->diffuse
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].specular"), l->specular
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].attenuation"), l->attenuation
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].intensity"), l->intensity
                );
                colorShader->SetUniform(
                  std::string("aux_lights[") + std::to_string(i) + std::string("].cast_shadow"), l->castShadow ? 1 : 0
                );
                for (int f = 0; f < 6; ++f) {
                    GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + f;
                    colorShader->SetUniform(
                      std::string("aux_lights[") + std::to_string(i) + std::string("].ProjectionViews[")
                        + std::to_string(f) + std::string("]"),
                      l->GetProjectionViewMatrix(0, face)
                    );
                }
            }
            colorShader->SetUniform(std::string("aux_light_count"), static_cast<int>(ctx->pointLights.size()));
            colorShader->SetUniform(std::string("shadow_map_unit"), (int)0);
            colorShader->SetUniform(std::string("omni_shadow_map_unit"), static_cast<int>(UNI_SHADOW_MAP_COUNT));
            colorShader->SetUniform(std::string("ProjectionView"), projectionView);
            // Surface parameters
            colorShader->SetUniform(std::string("surf_params.diffuse"), material->diffuse);
            colorShader->SetUniform(std::string("surf_params.specular"), material->specular);
            colorShader->SetUniform(std::string("surf_params.ambient"), material->ambient);
            colorShader->SetUniform(std::string("surf_params.shininess"), material->shininess);

            // Material textures - dynamically bound to Units 2-6
            // Base Map (Unit 2)
            glActiveTexture(GL_TEXTURE2);
            TextureHandle baseMap = material->baseMap;
            if (baseMap.IsValid() && static_cast<uint32_t>(baseMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(baseMap));
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("base_map_unit"), 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            TextureHandle normalMap = material->normalMap;
            if (normalMap.IsValid() && static_cast<uint32_t>(normalMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(normalMap));
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("normal_map_unit"), 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            TextureHandle aoMap = material->aoMap;
            if (aoMap.IsValid() && static_cast<uint32_t>(aoMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(aoMap));
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("ao_map_unit"), 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            TextureHandle roughnessMap = material->roughnessMap;
            if (roughnessMap.IsValid() && static_cast<uint32_t>(roughnessMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(roughnessMap));
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("roughness_map_unit"), 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            TextureHandle metallicMap = material->metallicMap;
            if (metallicMap.IsValid() && static_cast<uint32_t>(metallicMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(metallicMap));
            } else if (assetManager.GetDefaultTextures().size() > 4) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[4]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("metallic_map_unit"), 6);

            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
            // GLES 3 / WebGL 2.0 fallback: Non-instanced draw calls using World uniform
            AE_GL_PROBE(renderer, "Opaque pass: PRIM-GLES before non-instanced loop");
            for (const auto& inst : instances) {
                colorShader->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                  GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
                AE_GL_PROBE(renderer, "Opaque pass: PRIM-GLES after glDrawElements");
            }
#else
            // Upload batched instance data
            if (!instances.empty()) {
                glBufferData(
                  GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                );
                AE_GL_PROBE(renderer, "Opaque pass: PRIM after upload instance VBO");
                glDrawElementsInstanced(
                  GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
                AE_GL_PROBE(renderer, "Opaque pass: PRIM after instanced draw");
            }
#endif

            glBindVertexArray(0);
            break;
        }
    }

    ctx->_debugLineCount = ctx->debugLines.size() / 2;
    if (ctx->debugLines.size() > 0) {
        // glDisable(GL_DEPTH_TEST);
        auto debugShader = ctx->GetShader("debug_line");
        debugShader->Activate();
        debugShader->SetUniform(std::string("ProjectionView"), projectionView);

        renderer.debugBuffer->Upload(ctx->debugLines.data(), ctx->debugLines.size(), sizeof(DebugVertex));
        renderer.debugBuffer->Draw(enc, PrimitiveTopology::Lines);

        // glEnable(GL_DEPTH_TEST);

        ctx->debugLines.clear();
        ctx->_debugLineCount = 0;
    }

    renderer.CheckErrors("Opaque pass");
}

void DeferredGeometryPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("DeferredGeometryPass");
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gBuffer.id);
    std::array<GLuint, 4> attachments = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(attachments.size(), attachments.data());
    auto& assetManager = AssetManager::Get();

    glClearColor(renderer.clearColor.r, renderer.clearColor.g, renderer.clearColor.b, renderer.clearColor.a);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto geometryShader = ctx->GetShader("geometry");
    geometryShader->Activate();

    std::vector<RenderBatch> batches;

    if (!renderer.GetOpaqueQueue().empty()) {
        batches = BuildBatches(renderer.GetOpaqueQueue());
    }
    for (const auto& batch : batches) {
        Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
        const auto& instances = batch.instances;

        if (!mesh || !mesh->initialized) throw std::runtime_error("Mesh uninitialized!");

        Material* material = ResolveMaterialOrFallback(mesh->GetMaterial());

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (material->cullFaceEnabled)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);

        geometryShader->SetUniform(
          "ProjectionView", ctx->GetMainCamera()->GetProjectionMatrix() * ctx->GetMainCamera()->GetViewMatrix()
        );

        switch (mesh->type) {
        case MeshType::TERRAIN:
            // TODO: implement terrain rendering
            break;
        case MeshType::SKY:
            // TODO: implement skybox rendering
            break;
        case MeshType::VOXEL:
            // Handled by VoxelChunkPass.
            break;
        case MeshType::PRIM:
        default:
            // Material textures - dynamically bound to Units 2-6
            // Base Map (Unit 2)
            glActiveTexture(GL_TEXTURE2);
            TextureHandle baseMap = material->baseMap;
            if (baseMap.IsValid() && static_cast<uint32_t>(baseMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(baseMap));
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("baseMap", 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            TextureHandle normalMap = material->normalMap;
            if (normalMap.IsValid() && static_cast<uint32_t>(normalMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(normalMap));
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("normalMap", 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            TextureHandle aoMap = material->aoMap;
            if (aoMap.IsValid() && static_cast<uint32_t>(aoMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(aoMap));
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("aoMap", 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            TextureHandle roughnessMap = material->roughnessMap;
            if (roughnessMap.IsValid() && static_cast<uint32_t>(roughnessMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(roughnessMap));
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("roughnessMap", 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            TextureHandle metallicMap = material->metallicMap;
            if (metallicMap.IsValid() && static_cast<uint32_t>(metallicMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(metallicMap));
            } else if (assetManager.GetDefaultTextures().size() > 4) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[4]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("metallicMap", 6);

            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            if (!instances.empty()) {
                glBufferData(
                  GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                );
                glDrawElementsInstanced(
                  GetGLPrimitiveType(material->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
            }
            glBindVertexArray(0);
        }
    }

    renderer.CheckErrors("Geometry pass");
}

void DeferredLightingPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("DeferredLightingPass");
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    glClearColor(renderer.clearColor.r, renderer.clearColor.g, renderer.clearColor.b, renderer.clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto lightingShader = ctx->GetShader("lighting");
    lightingShader->Activate();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.gBuffer.positionRT);
    lightingShader->SetUniform("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.gBuffer.normalRT);
    lightingShader->SetUniform("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.gBuffer.albedoRT);
    lightingShader->SetUniform("gAlbedo", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer.gBuffer.materialRT);
    lightingShader->SetUniform("gMaterial", 3);

    auto mainLight = ctx->GetMainLight();
    lightingShader->SetUniform("cam_pos", ctx->GetMainCamera()->GetEyePosition());
    lightingShader->SetUniform("mainLight.direction", mainLight->direction);
    lightingShader->SetUniform("mainLight.ambient", mainLight->ambient);
    lightingShader->SetUniform("mainLight.diffuse", mainLight->diffuse);
    lightingShader->SetUniform("mainLight.specular", mainLight->specular);
    lightingShader->SetUniform("mainLight.intensity", mainLight->intensity);

    for (int i = 0; i < ctx->pointLights.size(); ++i) {
        LightComponent* l = ctx->pointLights[i];
        std::string prefix = "pointLights[" + std::to_string(i) + "]";
        lightingShader->SetUniform(prefix + ".position", l->GetPosition());
        lightingShader->SetUniform(prefix + ".ambient", l->ambient);
        lightingShader->SetUniform(prefix + ".diffuse", l->diffuse);
        lightingShader->SetUniform(prefix + ".specular", l->specular);
        lightingShader->SetUniform(prefix + ".attenuation", l->attenuation);
        lightingShader->SetUniform(prefix + ".intensity", l->intensity);
    }
    lightingShader->SetUniform("pointLightCount", static_cast<int>(ctx->pointLights.size()));

    renderer.screenBuffer->Draw(enc, PrimitiveTopology::TriangleStrip);

    renderer.CheckErrors("Lighting pass");
}

void TransparentPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("TransparentPass");
    auto& cam = *ctx->GetMainCamera();
    Atmospheric::CameraInfo camInfo = { .view = cam.GetViewMatrix(),
                                        .projection = cam.GetProjectionMatrix(),
                                        .position = cam.GetEyePosition() };
    Atmospheric::ParticleSubsystem::GetInstance().Draw(camInfo);// TODO: transparent pass
}

void MSAAResolvePass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("MSAAResolvePass");
    
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
 
    // Resolve MSAA color + depth — both RTs now use GL_DEPTH_COMPONENT32F.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (renderer.glesResolvedDepthFBO != 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.glesResolvedDepthFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.finalFBO);

    renderer.CheckErrors("MSAA resolve pass");
}

// WorldCanvasPass: World sprites with depth testing
void WorldCanvasPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("WorldCanvasPass");

    // Filter all world-space drawables (3D layers only, below LAYER_WORLD_2D)
    std::vector<CanvasDrawable*> worldDrawables;
    for (auto* drawable : ctx->canvasDrawables) {
        if (!drawable->gameObject->isActive) continue;
        if (static_cast<int>(drawable->GetLayer()) < static_cast<int>(CanvasLayer::LAYER_WORLD_2D)) {
            worldDrawables.push_back(drawable);
        }
    }

    if (worldDrawables.empty()) return;

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());

    // Get camera for projection
    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;

    glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();

    // Enable depth test (read only, don't write) so sprites are occluded by 3D geometry
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);// Read depth but don't write
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    // Sort by layer first, then by distance (back to front for transparency)
    glm::vec3 camPos = camera->GetEyePosition();
    std::sort(worldDrawables.begin(), worldDrawables.end(), [&camPos](CanvasDrawable* a, CanvasDrawable* b) {
        if (a->GetLayer() != b->GetLayer()) {
            return a->GetLayer() < b->GetLayer();
        }
        float distA = glm::length(a->gameObject->GetPosition() - camPos);
        float distB = glm::length(b->gameObject->GetPosition() - camPos);
        return distA > distB;// Back to front
    });

    renderer.GetBatchRenderer()->BeginBatch(viewProj);
    for (auto* drawable : worldDrawables) {
        drawable->Draw(renderer.GetBatchRenderer());
    }
    renderer.GetBatchRenderer()->EndBatch();

    // Restore depth mask
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    renderer.CheckErrors("WorldCanvas pass");
}

// CanvasPass: Pure 2D sprites (no depth testing)
void CanvasPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("CanvasPass");

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        GPUCanvasPass* gpuPass = renderer.GetGPUCanvasPass();
        if (!gpuPass) return;

        ctx->FlushTextToQueue(); // convert text commands → BatchDrawCommands in queue

        if (renderer.GetCanvasQueue().empty()) return;

        auto [width, height] = Window::Get()->GetPhysicalSize();
        const glm::mat4 viewProj = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1.0f, 1.0f);

        WGPUTextureView targetView = GfxFactory::GetCurrentSwapchainView();
        if (!targetView) return; // surface not ready (device still initializing)

        gpuPass->Render(targetView, viewProj, renderer.GetCanvasQueue());

        wgpuTextureViewRelease(targetView);
        GfxFactory::PresentSwapchain();
        return;
    }
#endif

    std::vector<CanvasDrawable*> drawables2D;
    for (auto* drawable : ctx->canvasDrawables) {
        if (!drawable->gameObject->isActive) continue;
        if (drawable->GetLayer() < CanvasLayer::LAYER_UI_BACK) {
            drawables2D.push_back(drawable);
        }
    }

    // Sort 2D drawables by layer first, then by z-order
    std::sort(drawables2D.begin(), drawables2D.end(), [](CanvasDrawable* a, CanvasDrawable* b) {
        if (a->GetLayer() != b->GetLayer()) {
            return a->GetLayer() < b->GetLayer();
        }
        return a->GetZOrder() < b->GetZOrder();
    });

    if (drawables2D.empty() && renderer.GetCanvasQueue().empty()) return;

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    glm::mat4 worldViewProj;
    CameraComponent* camera = ctx->GetMainCamera();
    if (camera && camera->IsOrthographic()) {
        // NOTES: by default, 2D sprites are rendered with a world space orthographic camera
        worldViewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    } else {
        // Fallback or perspective camera handling for 2D?
        // For now, if perspective, we might just use screen space or a default ortho.
        // Let's assume default ortho for safety if no 2D camera is set.
        auto [winW, winH] = Window::Get()->GetLogicalSize();
        worldViewProj = glm::ortho(0.0f, static_cast<float>(winW), static_cast<float>(winH), 0.0f, -1.0f, 1.0f);
    }

    renderer.GetBatchRenderer()->BeginBatch(worldViewProj);
    for (auto drawable : drawables2D) {
        if (!drawable->gameObject->isActive) continue;
        drawable->Draw(renderer.GetBatchRenderer());
    }
    for (const auto& cmd : renderer.GetCanvasQueue()) {
        renderer.GetBatchRenderer()->DrawGeometry(cmd.vertices, cmd.indices, cmd.textureID, cmd.transform);
    }
    renderer.GetBatchRenderer()->EndBatch();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

// Helper to reduce code duplication


void PostProcessPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("PostProcessPass");

    auto size = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, size.width, size.height);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.finalFBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.msaaResolveRT->GetTextureID());

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const char* shaderName = "hdr";
    switch (postEffect) {
        case PostEffect::CRT:          shaderName = "post_crt";           break;
        case PostEffect::VHS:          shaderName = "post_vhs";           break;
        case PostEffect::ColorGrading: shaderName = "post_color_grading"; break;
        case PostEffect::Posterize:    shaderName = "post_posterize";     break;
        case PostEffect::Sobel:        shaderName = "post_sobel";         break;
        case PostEffect::Edges:        shaderName = "post_edges";         break;
        case PostEffect::Vignette:     shaderName = "post_vignette";      break;
        default: break;
    }

    auto shader = ctx->GetShader(shaderName);
    shader->Activate();
    shader->SetUniform(std::string("color_map_unit"), (int)0);
    shader->SetUniform(std::string("exposure"),       tonemapEnabled ? exposure : 1.0f);
    shader->SetUniform(std::string("u_time"),         renderer.frameTime);
    shader->SetUniform(std::string("u_ca_enabled"),   static_cast<int>(caEnabled));
    shader->SetUniform(std::string("u_ca_strength"),  caStrength);

    renderer.screenBuffer->Draw(enc, PrimitiveTopology::TriangleStrip);

    renderer.CheckErrors("PostProcess pass");
}

void Renderer::SubmitUICommand(const BatchDrawCommand& cmd) {
    _hudQueue.push_back(cmd);
}

void Renderer::SubmitCanvasCommand(const BatchDrawCommand& cmd) {
    _canvasQueue.push_back(cmd);
}

void UIPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("UIPass");
    auto* batchRenderer = renderer.GetBatchRenderer();
    auto& queue = renderer.GetUIQueue();

    // Setup OpenGL state for UI
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // RmlUi context dimensions are in logical pixels, so projection must match.
    auto logicalSize = Window::Get()->GetLogicalSize();
    float width = static_cast<float>(logicalSize.width);
    float height = static_cast<float>(logicalSize.height);

    glViewport(0, 0, Window::Get()->GetPhysicalSize().width, Window::Get()->GetPhysicalSize().height);
    glm::mat4 projection = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);

    batchRenderer->BeginBatch(projection);

    for (const auto& cmd : queue) {
        batchRenderer->DrawGeometry(cmd.vertices, cmd.indices, cmd.textureID, cmd.transform);
    }

    ctx->RenderBufferedText(batchRenderer);

    batchRenderer->EndBatch();

    // Restore state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Renderer::readPixelsAsync(PixelReadbackCallback callback) {
    if (!callback) return;

    // Prefer the resolved (non-MSAA) target; fall back to the main scene RT.
    RenderTarget* src = msaaResolveRT ? msaaResolveRT.get() : sceneRT.get();
    if (!src) return;

    int w = src->GetWidth();
    int h = src->GetHeight();

    GpuImageData img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.channelCount = 4;
    img.data.resize(static_cast<size_t>(w) * h * 4);

    // Attach the color texture to a temporary FBO so glReadPixels can read it.
    GLuint tmpFBO = 0;
    glGenFramebuffers(1, &tmpFBO);

    GLint prevReadFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           static_cast<GLuint>(src->GetTextureID()), 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.data.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFBO));
    glDeleteFramebuffers(1, &tmpFBO);

    // Flip rows: OpenGL origin is bottom-left; callers expect top-to-bottom.
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> flipped(img.data.size());
    for (int row = 0; row < h; ++row) {
        std::memcpy(
            flipped.data() + static_cast<size_t>(row) * rowBytes,
            img.data.data() + static_cast<size_t>(h - 1 - row) * rowBytes,
            rowBytes);
    }
    img.data = std::move(flipped);

    callback(img);
}

// ─── Async PBO readback (Phase 1 / video recording path) ────────────────────

void Renderer::schedulePixelReadback() {
    RenderTarget* src = msaaResolveRT ? msaaResolveRT.get() : sceneRT.get();
    if (!src) return;

    const uint32_t w = static_cast<uint32_t>(src->GetWidth());
    const uint32_t h = static_cast<uint32_t>(src->GetHeight());
    const size_t   bufSize = static_cast<size_t>(w) * h * 4;

    // Lazy-init or re-init when resolution changes.
    if (m_readbackFBO == 0 || w != m_readbackPBOWidth || h != m_readbackPBOHeight) {
        destroyReadbackPBOs();

        glGenFramebuffers(1, &m_readbackFBO);
        glGenBuffers(READBACK_PBO_COUNT, m_readbackPBOs);
        for (int i = 0; i < READBACK_PBO_COUNT; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufSize),
                         nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        m_readbackPBOWidth  = w;
        m_readbackPBOHeight = h;
        m_readbackFrameIdx  = 0;
    }

    const int writeIdx = static_cast<int>(m_readbackFrameIdx % READBACK_PBO_COUNT);

    GLint prevReadFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readbackFBO);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           static_cast<GLuint>(src->GetTextureID()), 0);

    // nullptr offset = write into the bound PBO; returns immediately (async DMA).
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[writeIdx]);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFBO));

    ++m_readbackFrameIdx;
}

std::optional<GpuImageData> Renderer::collectPixelReadback() {
    // Pipeline needs READBACK_PBO_COUNT frames to prime before any data is ready.
    if (m_readbackFBO == 0 || m_readbackFrameIdx < static_cast<uint32_t>(READBACK_PBO_COUNT))
        return std::nullopt;

    const uint32_t w       = m_readbackPBOWidth;
    const uint32_t h       = m_readbackPBOHeight;
    const size_t   bufSize = static_cast<size_t>(w) * h * 4;

    // After schedulePixelReadback() incremented m_readbackFrameIdx, the oldest
    // in-flight PBO is at index m_readbackFrameIdx % READBACK_PBO_COUNT.
    // The GPU finished its DMA into this slot (READBACK_PBO_COUNT-1) frames ago.
    const int readIdx = static_cast<int>(m_readbackFrameIdx % READBACK_PBO_COUNT);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[readIdx]);
    const auto* gpuPtr = static_cast<const uint8_t*>(
        glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                         static_cast<GLsizeiptr>(bufSize), GL_MAP_READ_BIT));

    std::optional<GpuImageData> result;
    if (gpuPtr) {
        GpuImageData img;
        img.width        = w;
        img.height       = h;
        img.channelCount = 4;
        img.bottomUp     = true; // raw GL data; caller uses negative sws_scale stride
        img.data.assign(gpuPtr, gpuPtr + bufSize);
        result = std::move(img);
    }

    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return result;
}

void Renderer::destroyReadbackPBOs() {
    if (m_readbackFBO) {
        glDeleteFramebuffers(1, &m_readbackFBO);
        m_readbackFBO = 0;
    }
    if (m_readbackPBOs[0]) {
        glDeleteBuffers(READBACK_PBO_COUNT, m_readbackPBOs);
        for (auto& pbo : m_readbackPBOs) pbo = 0;
    }
    m_readbackFrameIdx  = 0;
    m_readbackPBOWidth  = 0;
    m_readbackPBOHeight = 0;
}

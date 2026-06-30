#include "renderer.hpp"
#include <cstring>
#include "asset_manager.hpp"
#include "batch_renderer_2d.hpp"
#include "canvas_drawable.hpp"
#include "console.hpp"
#include "game_object.hpp"
#include "gfx_factory.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "graphics_server.hpp"
#include "particle_server.hpp"
#include "physics_server_2d.hpp"
#include "window.hpp"
#include <algorithm>
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "command_encoder.hpp"
#include <webgpu/webgpu.h>
#include "renderer_wgsl.hpp"
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
    Mesh* mesh = nullptr;
    std::vector<InstanceData> instances;
};

static std::vector<RenderBatch> BuildBatches(const std::vector<Renderer::SortableCommand>& queue) {
    std::vector<RenderBatch> batches;
    RenderBatch currentBatch;
    currentBatch.mesh = queue[0].cmd.mesh;// TODO: maybe check if queue is empty

    for (const auto& sortable : queue) {
        const auto& cmd = sortable.cmd;

        if (currentBatch.mesh != cmd.mesh) {
            if (currentBatch.mesh != nullptr) {
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

void RenderGraph::AddPass(std::unique_ptr<RenderPass> pass) {
    _passes.push_back(std::move(pass));
}

void RenderGraph::Render(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    // Execute all passes in order (sorting, batching, drawing)
    for (auto& pass : _passes) {
        pass->Execute(ctx, renderer, enc);
    }
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
    // GL-only: no GL context exists when running the WebGPU backend.
    if (GfxFactory::GetBackend() != GfxBackend::WebGPU) {
        static const float quadVerts[] = {
            -1.f, -1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 1.f,
        };
        GLuint vbo;
        glGenVertexArrays(1, &gl.screenQuadVAO);
        glGenBuffers(1, &vbo);
        glBindVertexArray(gl.screenQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);

        // ── Skybox cube VAO ───────────────────────────────────────────
        static const float cubeVerts[] = {
            -1, -1, -1,  1, -1, -1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1, -1, -1,
            -1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1,  1,
            -1,  1,  1, -1,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1, -1,  1,  1,
             1,  1,  1,  1,  1, -1,  1, -1, -1,  1, -1, -1,  1, -1,  1,  1,  1,  1,
            -1, -1, -1, -1, -1,  1,  1, -1,  1,  1, -1,  1,  1, -1, -1, -1, -1, -1,
            -1,  1, -1, -1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, -1, -1,  1, -1,
        };
        glGenVertexArrays(1, &gl.skyboxVAO);
        glGenBuffers(1, &gl.skyboxVBO);
        glBindVertexArray(gl.skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gl.skyboxVBO);
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
    if (GfxFactory::GetBackend() != GfxBackend::WebGPU) {
        glDeleteVertexArrays(1, &gl.canvasVAO);
        glDeleteBuffers(1, &gl.canvasVBO);
        glDeleteVertexArrays(1, &gl.skyboxVAO);
        glDeleteBuffers(1, &gl.skyboxVBO);
    }
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

uint64_t Renderer::CalculateSortKey(const RenderCommand& cmd, const glm::vec3& cameraPos) {
    Material* mat = cmd.mesh->GetMaterial();
    if (!mat) return 0;

    // Calculate depth (distance from camera)
    glm::vec3 objPos = glm::vec3(cmd.transform[3]);
    float depth = glm::length(objPos - cameraPos);

    // Get render queue
    int renderQueue = mat->GetFinalRenderQueue();

    // Get material and mesh IDs for batching
    // TODO: Add proper ID system to Material and Mesh
    uint32_t materialID = reinterpret_cast<uintptr_t>(mat) & 0xFFFF;
    uint32_t meshID = reinterpret_cast<uintptr_t>(cmd.mesh) & 0xFFFF;

    // Generate 64-bit sort key
    // [16 bits: render queue] [16 bits: depth] [16 bits: material] [16 bits: mesh]
    uint64_t key = 0;
    key |= (uint64_t)(renderQueue & 0xFFFF) << 48;
    key |= (uint64_t)((uint16_t)(depth * 100.0f) & 0xFFFF) << 32;
    key |= (uint64_t)(materialID & 0xFFFF) << 16;
    key |= (uint64_t)(meshID & 0xFFFF);

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
        Material* mat = cmd.mesh->GetMaterial();
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

void Renderer::RenderFrame(GraphicsServer* ctx, float dt) {
    ZoneScopedN("Renderer::RenderFrame");
    AE_GL_PROBE(*this, "RenderFrame: entry");
#if defined(__APPLE__) && TARGET_OS_IOS
    // SDL3 iOS GLES view uses its own FBO (not 0). Query each frame.
    {
        SDL_Window* win = static_cast<SDL_Window*>(Window::Get()->GetNativeHandle());
        SDL_PropertiesID props = SDL_GetWindowProperties(win);
        Sint64 fb = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
        gl.finalFBO = static_cast<GLuint>(fb);
    }
    AE_GL_PROBE(*this, "RenderFrame: after query iOS FBO");
#endif
    frameTime += dt;
    SortAndBucket(ctx->GetMainCamera()->GetEyePosition());

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        GPUCommandEncoder gpuEnc;
        gpuEnc.encoder = wgpuDeviceCreateCommandEncoder(GfxFactory::GetWebGPUDevice(), nullptr);

        sceneRT->Clear(clearColor);
        _renderGraph->Render(ctx, *this, &gpuEnc);

        WGPUCommandBufferDescriptor cmdDesc{};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(gpuEnc.encoder, &cmdDesc);
        wgpuQueueSubmit(GfxFactory::GetWebGPUQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(gpuEnc.encoder);

        GfxFactory::PresentSwapchain();
    } else
#endif
    {
        _renderGraph->Render(ctx, *this);
    }

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
        Console::Get()->Error(fmt::format("{}: {}\n", prefix, error));
    }
}

void Renderer::CreateFBOs() {
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
    glGenFramebuffers(1, &gl.shadowFBO);
    glGenFramebuffers(1, &gl.gBuffer.id);
}

void Renderer::DestroyFBOs() {
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
    glDeleteFramebuffers(1, &gl.shadowFBO);
    glDeleteFramebuffers(1, &gl.gBuffer.id);
}

void Renderer::CreateRTs(const RenderTargetProps& props) {
    const bool isGL = GfxFactory::GetBackend() != GfxBackend::WebGPU;

    // 1. Create and set shadow pass attachments
    if (isGL) {
    for (int i = 0; i < MAX_UNI_LIGHTS; ++i) {
        GLuint map;
        glGenTextures(1, &map);
        glBindTexture(GL_TEXTURE_2D, map);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        gl.uniShadowMaps[i] = map;
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
              NULL
            );
        }
        gl.omniShadowMaps[i] = map;
    }

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    glBindFramebuffer(GL_FRAMEBUFFER, gl.shadowFBO);
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#else
    glBindFramebuffer(GL_FRAMEBUFFER, gl.shadowFBO);
    for (int i = 0; i < (int)gl.uniShadowMaps.size(); ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.uniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    for (int i = 0; i < (int)gl.omniShadowMaps.size(); ++i) {
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, gl.omniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
#endif
    CheckErrors("Create shadow RTs");
    } // isGL

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
    if (isGL && sceneRT && sceneRT->GetNumSamples() > 1) {
        glGenTextures(1, &gl.glesResolvedDepthTex);
        glBindTexture(GL_TEXTURE_2D, gl.glesResolvedDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &gl.glesResolvedDepthFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, gl.glesResolvedDepthFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.glesResolvedDepthTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Console::Get()->Error("Renderer: WebGL resolved depth FBO incomplete!");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, gl.finalFBO);

        gl.glesResolvedDepthWidth = props.width;
        gl.glesResolvedDepthHeight = props.height;
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
    if (isGL) {
    glGenTextures(1, &gl.gBuffer.positionRT);
    glBindTexture(GL_TEXTURE_2D, gl.gBuffer.positionRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gl.gBuffer.normalRT);
    glBindTexture(GL_TEXTURE_2D, gl.gBuffer.normalRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gl.gBuffer.albedoRT);
    glBindTexture(GL_TEXTURE_2D, gl.gBuffer.albedoRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gl.gBuffer.materialRT);
    glBindTexture(GL_TEXTURE_2D, gl.gBuffer.materialRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gl.gBuffer.depthRT);
    glBindTexture(GL_TEXTURE_2D, gl.gBuffer.depthRT);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
    );

    glGenFramebuffers(1, &gl.gBuffer.id);
    glBindFramebuffer(GL_FRAMEBUFFER, gl.gBuffer.id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.gBuffer.positionRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl.gBuffer.normalRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gl.gBuffer.albedoRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, gl.gBuffer.materialRT, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.gBuffer.depthRT, 0);
    std::array<GLuint, 4> attachments = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(attachments.size(), attachments.data());
    CheckFramebufferStatus("G-buffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, gl.finalFBO);
    CheckErrors("Create G-buffer RTs");
    } // isGL
}

void Renderer::DestroyRTs() {
    sceneRT.reset();
    msaaResolveRT.reset();

    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (gl.glesResolvedDepthFBO) {
        glDeleteFramebuffers(1, &gl.glesResolvedDepthFBO);
        gl.glesResolvedDepthFBO = 0;
    }
    if (gl.glesResolvedDepthTex) {
        glDeleteTextures(1, &gl.glesResolvedDepthTex);
        gl.glesResolvedDepthTex = 0;
    }
    gl.glesResolvedDepthWidth = 0;
    gl.glesResolvedDepthHeight = 0;
#endif

    glDeleteTextures(1, &gl.gBuffer.positionRT);
    glDeleteTextures(1, &gl.gBuffer.normalRT);
    glDeleteTextures(1, &gl.gBuffer.albedoRT);
    glDeleteTextures(1, &gl.gBuffer.materialRT);
    glDeleteTextures(1, &gl.gBuffer.depthRT);
}

void Renderer::CreateCanvasVAO() {
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
    glGenVertexArrays(1, &gl.canvasVAO);
    glGenBuffers(1, &gl.canvasVBO);

    glBindVertexArray(gl.canvasVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gl.canvasVBO);
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

void ShadowPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
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
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.shadowFBO);

    auto mainLight = ctx->GetMainLight();

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderer.gl.uniShadowMaps[0], 0);
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
        Mesh* mesh = batch.mesh;
        const auto& instances = batch.instances;// NOTES: no need to check for empty instances here, as we only add
                                                // batches with instances (and OpenGL will handle 0 instances whatever)

        if (!mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (mesh->GetMaterial()->cullFaceEnabled)
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
                  GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
            }
#else
            // Upload ALL instances for this batch once
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW);

            // Instanced Draw
            glDrawElementsInstanced(
              GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
            );
#endif
        }
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.finalFBO);

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
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, face, renderer.gl.omniShadowMaps[i], 0);
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
                Mesh* mesh = batch.mesh;
                const auto& instances = batch.instances;

                if (!mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);

                if (mesh->GetMaterial()->cullFaceEnabled)
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
                          GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
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
                      GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                    );
#endif
                }
                glBindVertexArray(0);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.finalFBO);
}

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
ForwardOpaquePass::~ForwardOpaquePass() {
    for (auto& [id, bg] : _texBGCache) wgpuBindGroupRelease(bg);
    if (_uniformBG)       wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL)      wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL)          wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline)        wgpuRenderPipelineRelease(_pipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf)  wgpuBufferRelease(_drawUniformBuf);
    if (_whiteTex)        wgpuTextureRelease(_whiteTex);
    if (_sampler)         wgpuSamplerRelease(_sampler);
}

void ForwardOpaquePass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat) {
    _gpuDevice = device;
    _gpuQueue  = queue;

    {
        WGPUBufferDescriptor d{};
        d.size  = FWD_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Pass-owned 1x1 white fallback texture — AssetManager::GetDefaultTextures()
    // is GL-only, so meshes with no base map sample this instead.
    {
        WGPUTextureDescriptor d{};
        d.size          = { 1, 1, 1 };
        d.format        = WGPUTextureFormat_RGBA8Unorm;
        d.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension     = WGPUTextureDimension_2D;
        d.mipLevelCount = 1;
        d.sampleCount   = 1;
        _whiteTex = wgpuDeviceCreateTexture(device, &d);
        const uint8_t white[4] = { 255, 255, 255, 255 };
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _whiteTex;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, white, 4, &layout, &extent);
    }
    {
        WGPUSamplerDescriptor d{};
        d.minFilter    = WGPUFilterMode_Linear;
        d.magFilter    = WGPUFilterMode_Linear;
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.addressModeU = WGPUAddressMode_Repeat;
        d.addressModeV = WGPUAddressMode_Repeat;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    // Standard Vertex (vertex.hpp): pos(vec3)@0, uv(vec2)@12, normal(vec3)@20,
    // tangent(vec3)@32, bitangent(vec3)@44 — stride 56. Tangent/bitangent are
    // unused by this simplified (no normal-mapping) shader but stay in the
    // buffer layout since the underlying Buffer is shared with the GL path.
    auto p = GpuPipelineBuilder(device)
        .wgsl(FORWARD_OPAQUE_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::both, FWD_FRAME_UNIFORM_SIZE),
               gpuDynUniform(1, wgsl_stage::both, FWD_DRAW_UNIFORM_SIZE) })
        .bgl({ gpuTexture(0), gpuSampler(1) })
        .vertex(56, { {WGPUVertexFormat_Float32x3,  0, 0},
                      {WGPUVertexFormat_Float32x2, 12, 1},
                      {WGPUVertexFormat_Float32x3, 20, 2} })
        .colorFormat(colorFormat)
        .depth(true, WGPUCompareFunction_Less)
        .cull(WGPUCullMode_Back)
        .build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL     = p.bgl(1);
}

void ForwardOpaquePass::_ensureDrawCapacity(uint32_t drawCount) {
    if (drawCount <= _drawSlotCapacity && _drawUniformBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(drawCount, std::max<uint32_t>(_drawSlotCapacity * 2, 16));

    if (_drawUniformBuf) { wgpuBufferRelease(_drawUniformBuf); _drawUniformBuf = nullptr; }
    if (_uniformBG)      { wgpuBindGroupRelease(_uniformBG);   _uniformBG      = nullptr; }

    WGPUBufferDescriptor d{};
    d.size  = static_cast<uint64_t>(newCapacity) * FWD_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf   = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].buffer  = _frameUniformBuf;
    entries[0].size    = FWD_FRAME_UNIFORM_SIZE;
    entries[1].binding = 1;
    entries[1].buffer  = _drawUniformBuf;
    entries[1].size    = FWD_DRAW_UNIFORM_SIZE;
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout     = _uniformBGL;
    bgDesc.entryCount = 2;
    bgDesc.entries    = entries;
    _uniformBG = wgpuDeviceCreateBindGroup(_gpuDevice, &bgDesc);
}

WGPUBindGroup ForwardOpaquePass::_getOrCreateTexBG(uint32_t texID) {
    auto it = _texBGCache.find(texID);
    if (it != _texBGCache.end()) return it->second;

    WGPUTexture rawTex = (texID == 0) ? _whiteTex : GfxFactory::GetWGPUTexture(texID);
    if (!rawTex) rawTex = _whiteTex;

    WGPUTextureView view = wgpuTextureCreateView(rawTex, nullptr);

    WGPUBindGroupEntry e[2]{};
    e[0].binding     = 0;
    e[0].textureView = view;
    e[1].binding     = 1;
    e[1].sampler     = _sampler;
    WGPUBindGroupDescriptor d{};
    d.layout     = _texBGL;
    d.entryCount = 2;
    d.entries    = e;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(_gpuDevice, &d);

    wgpuTextureViewRelease(view);
    _texBGCache[texID] = bg;
    return bg;
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void ForwardOpaquePass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        const auto& gpuQueueCmds = renderer.GetOpaqueQueue();
        if (gpuQueueCmds.empty()) return;
        if (!renderer.sceneRT)    return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();

        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float);
        }

        struct DrawItem { Buffer* buf; glm::mat4 model; Material* mat; };
        std::vector<DrawItem> draws;
        draws.reserve(gpuQueueCmds.size());
        for (const auto& sortable : gpuQueueCmds) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = cmd.mesh;
            // TERRAIN (tessellation) has no WebGPU equivalent; VOXEL is handled
            // by VoxelChunkPass. Both are documented simplifications.
            if (!mesh || mesh->type != MeshType::PRIM) continue;
            if (!mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            draws.push_back({ buf, cmd.transform, mesh->GetMaterial() });
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir   = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
        glm::vec3 ambient    = light ? light->ambient : glm::vec3(0.1f);
        glm::mat4 viewProj   = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        struct {
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambient;
        } frameUniforms{
            viewProj,
            glm::vec4(camera->GetEyePosition(), 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(ambient, 0.0f),
        };
        wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &frameUniforms, sizeof(frameUniforms));

        // Pack every draw's per-instance data into its own dynamic-offset slot
        // and upload in a single call (see VOXEL_WGSL's comment).
        std::vector<uint8_t> drawData(draws.size() * FWD_DRAW_SLOT_STRIDE, 0);
        for (size_t i = 0; i < draws.size(); ++i) {
            Material* mat = draws[i].mat;
            glm::vec3 diffuse  = mat ? mat->diffuse  : glm::vec3(0.55f);
            glm::vec3 specular = mat ? mat->specular : glm::vec3(0.7f);
            glm::vec3 matAmbient = mat ? mat->ambient : glm::vec3(0.0f);
            float shininess    = mat ? mat->shininess : 0.25f;

            uint8_t* slot = drawData.data() + i * FWD_DRAW_SLOT_STRIDE;
            std::memcpy(slot,      &draws[i].model, sizeof(glm::mat4));
            glm::vec4 d4(diffuse, 0.0f), s4(specular, 0.0f), a4(matAmbient, 0.0f), sh4(shininess, 0.0f, 0.0f, 0.0f);
            std::memcpy(slot + 64, &d4,  sizeof(glm::vec4));
            std::memcpy(slot + 80, &s4,  sizeof(glm::vec4));
            std::memcpy(slot + 96, &a4,  sizeof(glm::vec4));
            std::memcpy(slot + 112, &sh4, sizeof(glm::vec4));
        }
        wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        renderer.sceneRT->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        for (size_t i = 0; i < draws.size(); ++i) {
            Material* mat = draws[i].mat;
            uint32_t texID = (mat && mat->baseMap.IsValid()) ? mat->baseMap.id : 0;
            WGPUBindGroup texBG = _getOrCreateTexBG(texID);

            uint32_t dynamicOffset = static_cast<uint32_t>(i * FWD_DRAW_SLOT_STRIDE);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 1, &dynamicOffset);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 1, texBG, 0, nullptr);
            draws[i].buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        renderer.sceneRT->End();
        return;
    }
#endif
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
        glBindTexture(GL_TEXTURE_2D, renderer.gl.uniShadowMaps[i]);
    }
    for (int i = 0; i < MAX_OMNI_LIGHTS; ++i) {
        glActiveTexture(GL_TEXTURE0 + UNI_SHADOW_MAP_COUNT + i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, renderer.gl.omniShadowMaps[i]);
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
        Mesh* mesh = batch.mesh;
        const auto& instances = batch.instances;

        if (!mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

        AE_GL_PROBE(renderer, fmt::format("Opaque pass: batch entry (type={})", (int)mesh->type));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
        if (renderer.wireframeEnabled || mesh->GetMaterial()->polygonMode == PolygonMode::Line)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

        if (mesh->GetMaterial()->cullFaceEnabled)
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

            terrainShader->SetUniform(std::string("surf_params.diffuse"), mesh->GetMaterial()->diffuse);
            terrainShader->SetUniform(std::string("surf_params.specular"), mesh->GetMaterial()->specular);
            terrainShader->SetUniform(std::string("surf_params.ambient"), mesh->GetMaterial()->ambient);
            terrainShader->SetUniform(std::string("surf_params.shininess"), mesh->GetMaterial()->shininess);

            const auto& td = mesh->terrainData.value_or(TerrainShaderData{});
            terrainShader->SetUniform(std::string("tessellation_factor"), td.tessellationFactor);
            terrainShader->SetUniform(std::string("height_scale"),         td.heightScale);
            glActiveTexture(GL_TEXTURE7);
            TextureHandle heightMap = mesh->GetMaterial()->heightMap;
            if (heightMap.IsValid() && (uint32_t)heightMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)heightMap);
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
            colorShader->SetUniform(std::string("aux_light_count"), (int)ctx->pointLights.size());
            colorShader->SetUniform(std::string("shadow_map_unit"), (int)0);
            colorShader->SetUniform(std::string("omni_shadow_map_unit"), (int)UNI_SHADOW_MAP_COUNT);
            colorShader->SetUniform(std::string("ProjectionView"), projectionView);
            // Surface parameters
            colorShader->SetUniform(std::string("surf_params.diffuse"), mesh->GetMaterial()->diffuse);
            colorShader->SetUniform(std::string("surf_params.specular"), mesh->GetMaterial()->specular);
            colorShader->SetUniform(std::string("surf_params.ambient"), mesh->GetMaterial()->ambient);
            colorShader->SetUniform(std::string("surf_params.shininess"), mesh->GetMaterial()->shininess);

            // Material textures - dynamically bound to Units 2-6
            // Base Map (Unit 2)
            glActiveTexture(GL_TEXTURE2);
            TextureHandle baseMap = mesh->GetMaterial()->baseMap;
            if (baseMap.IsValid() && (uint32_t)baseMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)baseMap);
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("base_map_unit"), 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            TextureHandle normalMap = mesh->GetMaterial()->normalMap;
            if (normalMap.IsValid() && (uint32_t)normalMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)normalMap);
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("normal_map_unit"), 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            TextureHandle aoMap = mesh->GetMaterial()->aoMap;
            if (aoMap.IsValid() && (uint32_t)aoMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)aoMap);
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("ao_map_unit"), 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            TextureHandle roughnessMap = mesh->GetMaterial()->roughnessMap;
            if (roughnessMap.IsValid() && (uint32_t)roughnessMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)roughnessMap);
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("roughness_map_unit"), 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            TextureHandle metallicMap = mesh->GetMaterial()->metallicMap;
            if (metallicMap.IsValid() && (uint32_t)metallicMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)metallicMap);
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
                  GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
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
                  GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
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

void DeferredGeometryPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("DeferredGeometryPass");
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.gBuffer.id);
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
        Mesh* mesh = batch.mesh;
        const auto& instances = batch.instances;

        if (!mesh->initialized) throw std::runtime_error("Mesh uninitialized!");

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (mesh->GetMaterial()->cullFaceEnabled)
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
            TextureHandle baseMap = mesh->GetMaterial()->baseMap;
            if (baseMap.IsValid() && (uint32_t)baseMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)baseMap);
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("baseMap", 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            TextureHandle normalMap = mesh->GetMaterial()->normalMap;
            if (normalMap.IsValid() && (uint32_t)normalMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)normalMap);
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("normalMap", 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            TextureHandle aoMap = mesh->GetMaterial()->aoMap;
            if (aoMap.IsValid() && (uint32_t)aoMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)aoMap);
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("aoMap", 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            TextureHandle roughnessMap = mesh->GetMaterial()->roughnessMap;
            if (roughnessMap.IsValid() && (uint32_t)roughnessMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)roughnessMap);
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("roughnessMap", 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            TextureHandle metallicMap = mesh->GetMaterial()->metallicMap;
            if (metallicMap.IsValid() && (uint32_t)metallicMap != 0) {
                glBindTexture(GL_TEXTURE_2D, (uint32_t)metallicMap);
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
                  GetGLPrimitiveType(mesh->GetMaterial()->primitiveType), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
            }
            glBindVertexArray(0);
        }
    }

    renderer.CheckErrors("Geometry pass");
}

void DeferredLightingPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("DeferredLightingPass");
    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    glClearColor(renderer.clearColor.r, renderer.clearColor.g, renderer.clearColor.b, renderer.clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto lightingShader = ctx->GetShader("lighting");
    lightingShader->Activate();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.gl.gBuffer.positionRT);
    lightingShader->SetUniform("gPosition", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.gl.gBuffer.normalRT);
    lightingShader->SetUniform("gNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.gl.gBuffer.albedoRT);
    lightingShader->SetUniform("gAlbedo", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, renderer.gl.gBuffer.materialRT);
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
    lightingShader->SetUniform("pointLightCount", (int)ctx->pointLights.size());

    renderer.screenBuffer->Draw(enc, PrimitiveTopology::TriangleStrip);

    renderer.CheckErrors("Lighting pass");
}

void TransparentPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    ZoneScopedN("TransparentPass");
    auto& cam = *ctx->GetMainCamera();
    Atmospheric::CameraInfo camInfo = { .view = cam.GetViewMatrix(),
                                        .projection = cam.GetProjectionMatrix(),
                                        .position = cam.GetEyePosition() };
    Atmospheric::ParticleServer::GetInstance().Draw(camInfo);// TODO: transparent pass
}

void MSAAResolvePass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    ZoneScopedN("MSAAResolvePass");

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);
 
    // Resolve MSAA color + depth — both RTs now use GL_DEPTH_COMPONENT32F.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (renderer.gl.glesResolvedDepthFBO != 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.gl.glesResolvedDepthFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.finalFBO);

    renderer.CheckErrors("MSAA resolve pass");
}

// WorldCanvasPass: World sprites with depth testing
void WorldCanvasPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        GPUCanvasPass* gpuPass = renderer.GetGPUCanvasPass();
        if (!gpuPass) return;

        // Same layer filter as the GL path below: 3D-space drawables only.
        std::vector<CanvasDrawable*> worldDrawables;
        for (auto* drawable : ctx->canvasDrawables) {
            if (!drawable->gameObject->isActive) continue;
            if ((int)drawable->GetLayer() < (int)CanvasLayer::LAYER_WORLD_2D)
                worldDrawables.push_back(drawable);
        }
        if (worldDrawables.empty()) return;
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        glm::vec3 camPos = camera->GetEyePosition();
        std::sort(worldDrawables.begin(), worldDrawables.end(), [&camPos](CanvasDrawable* a, CanvasDrawable* b) {
            if (a->GetLayer() != b->GetLayer()) return a->GetLayer() < b->GetLayer();
            float distA = glm::length(a->gameObject->GetPosition() - camPos);
            float distB = glm::length(b->gameObject->GetPosition() - camPos);
            return distA > distB; // Back to front
        });

        // Drain through BatchRenderer2D's CPU path, same as CanvasPass.
        std::vector<BatchDrawCommand> allCommands;
        auto* br = renderer.GetBatchRenderer();
        br->StartBatch();
        for (auto* drawable : worldDrawables)
            drawable->Draw(br);
        allCommands = br->DrainToCommands();
        if (allCommands.empty()) return;

        // Render into the already-open sceneRT pass (depth-tested, read-only)
        // so world sprites are occluded by — but never occlude — 3D geometry.
        renderer.sceneRT->Begin(enc);
        gpuPass->Render(enc, viewProj, allCommands, /*depthTest=*/true);
        renderer.sceneRT->End();
        return;
    }
#endif
    ZoneScopedN("WorldCanvasPass");

    // Filter all world-space drawables (3D layers only, below LAYER_WORLD_2D)
    std::vector<CanvasDrawable*> worldDrawables;
    for (auto* drawable : ctx->canvasDrawables) {
        if (!drawable->gameObject->isActive) continue;
        if ((int)drawable->GetLayer() < (int)CanvasLayer::LAYER_WORLD_2D) {
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
void CanvasPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("CanvasPass");

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        GPUCanvasPass* gpuPass = renderer.GetGPUCanvasPass();
        if (!gpuPass) return;

        ctx->FlushTextToQueue(); // convert text commands → BatchDrawCommands in queue

        // Collect 2D drawables (same layer filter as the GL path below)
        std::vector<CanvasDrawable*> drawables2D;
        for (auto* drawable : ctx->canvasDrawables) {
            if (!drawable->gameObject->isActive) continue;
            if (drawable->GetLayer() < CanvasLayer::LAYER_UI_BACK)
                drawables2D.push_back(drawable);
        }
        std::sort(drawables2D.begin(), drawables2D.end(), [](CanvasDrawable* a, CanvasDrawable* b) {
            if (a->GetLayer() != b->GetLayer()) return a->GetLayer() < b->GetLayer();
            return a->GetZOrder() < b->GetZOrder();
        });

        if (drawables2D.empty() && renderer.GetCanvasQueue().empty()) return;

        // Determine viewProj the same way as the GL path: prefer camera's orthographic
        // matrix so that world-space sprite positions map correctly.
        glm::mat4 viewProj;
        CameraComponent* camera = ctx->GetMainCamera();
        if (camera && camera->IsOrthographic()) {
            viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        } else {
            auto [winW, winH] = Window::Get()->GetLogicalSize();
            viewProj = glm::ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
        }

        // Drain canvasDrawables through BatchRenderer2D's CPU path (no GL calls)
        std::vector<BatchDrawCommand> allCommands;
        if (!drawables2D.empty()) {
            auto* br = renderer.GetBatchRenderer();
            br->StartBatch();
            for (auto* drawable : drawables2D)
                drawable->Draw(br);
            auto drained = br->DrainToCommands();
            allCommands.insert(allCommands.end(),
                               std::make_move_iterator(drained.begin()),
                               std::make_move_iterator(drained.end()));
        }
        // Append direct canvas queue (text, Lua commands, etc.)
        for (const auto& cmd : renderer.GetCanvasQueue())
            allCommands.push_back(cmd);

        if (allCommands.empty()) return;
        if (!renderer.sceneRT) return;

        // Record into the shared per-frame encoder/render pass on sceneRT —
        // PostProcessPass is the sole pass that later writes sceneRT to the
        // swapchain, so we must not touch the swapchain or present here.
        renderer.sceneRT->Begin(enc);
        gpuPass->Render(enc, viewProj, allCommands);
        renderer.sceneRT->End();
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
        worldViewProj = glm::ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
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

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
PostProcessPass::~PostProcessPass() {
    if (_texBG)      wgpuBindGroupRelease(_texBG);
    if (_uniformBG)  wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL)     wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline)   wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_sampler)    wgpuSamplerRelease(_sampler);
}

void PostProcessPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat swapchainFormat) {
    _gpuDevice = device;
    _gpuQueue  = queue;

    {
        WGPUBufferDescriptor d{};
        d.size  = 16; // exposure, caEnabled, caStrength, pad
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    {
        WGPUSamplerDescriptor d{};
        d.minFilter    = WGPUFilterMode_Linear;
        d.magFilter    = WGPUFilterMode_Linear;
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.addressModeU = WGPUAddressMode_ClampToEdge;
        d.addressModeV = WGPUAddressMode_ClampToEdge;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    auto p = GpuPipelineBuilder(device)
        .wgsl(POSTPROCESS_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::frag, 16) })
        .bgl({ gpuTexture(0), gpuSampler(1) })
        .colorFormat(swapchainFormat)
        .build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL     = p.bgl(1);

    {
        WGPUBindGroupEntry e{};
        e.binding = 0;
        e.buffer  = _uniformBuf;
        e.size    = 16;
        WGPUBindGroupDescriptor d{};
        d.layout     = _uniformBGL;
        d.entryCount = 1;
        d.entries    = &e;
        _uniformBG = wgpuDeviceCreateBindGroup(device, &d);
    }
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void PostProcessPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, GfxFactory::GetSwapchainFormat());
        }
        if (!renderer.sceneRT) return;

        auto* sceneRT = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
        WGPUTexture sceneTex = sceneRT->GetNativeTexture();
        if (!sceneTex) return;

        WGPUTextureView swapchainView = GfxFactory::GetCurrentSwapchainView();
        if (!swapchainView) return; // surface not ready (device still initializing)

        // Rebuild the texture bind group only when sceneRT's texture object
        // changes (e.g. on resize) — cheap to check, avoids a per-frame alloc.
        if (sceneTex != _texBGSource) {
            if (_texBG) wgpuBindGroupRelease(_texBG);
            WGPUTextureView sceneView = wgpuTextureCreateView(sceneTex, nullptr);
            WGPUBindGroupEntry e[2]{};
            e[0].binding     = 0;
            e[0].textureView = sceneView;
            e[1].binding     = 1;
            e[1].sampler     = _sampler;
            WGPUBindGroupDescriptor d{};
            d.layout     = _texBGL;
            d.entryCount = 2;
            d.entries    = e;
            _texBG = wgpuDeviceCreateBindGroup(_gpuDevice, &d);
            wgpuTextureViewRelease(sceneView);
            _texBGSource = sceneTex;
        }

        struct { float exposure, caEnabled, caStrength, pad; } u{
            tonemapEnabled ? exposure : 1.0f,
            caEnabled ? 1.0f : 0.0f,
            caStrength,
            0.0f
        };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        WGPURenderPassColorAttachment ca{};
        ca.view       = swapchainView;
        ca.loadOp     = WGPULoadOp_Clear;
        ca.storeOp    = WGPUStoreOp_Store;
        ca.clearValue = { renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w };
        WGPURenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments     = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &rpDesc);

        wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, _uniformBG, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, _texBG, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        wgpuTextureViewRelease(swapchainView);
        return;
    }
#endif
    ZoneScopedN("PostProcessPass");

    auto size = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, size.width, size.height);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.finalFBO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.msaaResolveRT->GetTextureID());

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto shader = ctx->GetShader("hdr");
    shader->Activate();
    shader->SetUniform(std::string("color_map_unit"), (int)0);
    shader->SetUniform(std::string("exposure"),       tonemapEnabled ? exposure : 1.0f);
    shader->SetUniform(std::string("u_ca_enabled"),   (int)caEnabled);
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

void UIPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // RenderBufferedText interleaves text glyphs with queued geometry inside
    // the same GL BatchRenderer2D Begin/EndBatch pass (for correct z-order),
    // so it can't be ported by simply swapping the draw calls — it would need
    // glyphs and geometry merged into one ordered BatchDrawCommand list before
    // handing off to GPUCanvasPass. No-op for now rather than risk an
    // incorrectly-ordered or partially-working port.
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
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
    float width = (float)logicalSize.width;
    float height = (float)logicalSize.height;

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

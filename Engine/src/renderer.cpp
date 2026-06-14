#include "renderer.hpp"
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
#include <webgpu/webgpu.h>
#endif
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

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

    // ── Skybox cube VAO ─────────────────────────────────────────────
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

    // debug and screen are now std::unique_ptr<Buffer> that auto-destruct
    glDeleteVertexArrays(1, &canvasVAO);
    glDeleteBuffers(1, &canvasVBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
}

void Renderer::Resize(int width, int height) {
    DestroyRTs();
    CreateRTs(RenderTargetProps{ width, height });
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
#ifndef __EMSCRIPTEN__
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
#ifndef __EMSCRIPTEN__
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
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
              NULL
            );
        }
        omniShadowMaps[i] = map;
    }

#ifdef __EMSCRIPTEN__
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#else
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    for (int i = 0; i < (int)uniShadowMaps.size(); ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, uniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    for (int i = 0; i < (int)omniShadowMaps.size(); ++i) {
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, omniShadowMaps[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
#endif
    CheckErrors("Create shadow RTs");

    // 2. Scene render target (MSAA on desktop forward path, standard otherwise)
    {
        RenderTarget::Props p;
        p.width = props.width;
        p.height = props.height;
        p.withDepth = true;
        p.hdr = true;
#ifndef __EMSCRIPTEN__
        if (_currRenderPath == RenderPath::Forward)
            p.numSamples = props.numSamples;
#endif
        sceneRT = GfxFactory::CreateRenderTarget(p);
    }

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.normalRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.normalRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.albedoRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.albedoRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.materialRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.materialRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.depthRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.depthRT);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    CheckErrors("Create G-buffer RTs");
}

void Renderer::DestroyRTs() {
    sceneRT.reset();
    msaaResolveRT.reset();

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

void ShadowPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("ShadowPass");
    glViewport(0, 0, SHADOW_W, SHADOW_H);
#ifndef __EMSCRIPTEN__
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
#ifdef __EMSCRIPTEN__
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

#ifdef __EMSCRIPTEN__
            // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
            for (const auto& inst : instances) {
                depthShader->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                  mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
            }
#else
            // Upload ALL instances for this batch once
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW);

            // Instanced Draw
            glDrawElementsInstanced(
              mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
            );
#endif
        }
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
#ifdef __EMSCRIPTEN__
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

#ifdef __EMSCRIPTEN__
                    // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
                    for (const auto& inst : instances) {
                        depthCubemapShader->SetUniform(std::string("World"), inst.modelMatrix);
                        glDrawElements(
                          mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
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
                      mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                    );
#endif
                }
                glBindVertexArray(0);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ForwardOpaquePass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("ForwardOpaquePass");
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
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
    // Global static binding removed; textures are now dynamically bound per draw call

    auto mainLight = ctx->GetMainLight();
    glm::vec3 eyePos = ctx->GetMainCamera()->GetEyePosition();
    glm::mat4 projectionView = ctx->GetMainCamera()->GetProjectionMatrix() * ctx->GetMainCamera()->GetViewMatrix();

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

#ifndef __EMSCRIPTEN__
        if (renderer.wireframeEnabled || mesh->GetMaterial()->polygonMode == GL_LINE)
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

            terrainShader->SetUniform(std::string("tessellation_factor"), (float)16.0);
            terrainShader->SetUniform(std::string("height_scale"), (float)32.0);
            glActiveTexture(GL_TEXTURE7);
            int heightMap = mesh->GetMaterial()->heightMap;
            if (heightMap >= 0 && (size_t)heightMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[heightMap]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            terrainShader->SetUniform(std::string("height_map_unit"), 7);
            terrainShader->SetUniform(std::string("ProjectionView"), projectionView);

            // Terrain is usually a single instance, but we handle it in the batch loop
            // Assuming terrain is not instanced for now or handled as single instance
            terrainShader->SetUniform(std::string("World"), instances[0].modelMatrix);

            glBindVertexArray(mesh->vao);
#ifndef __EMSCRIPTEN__
            glDrawArrays(GL_PATCHES, 0, mesh->vertCount);
#else
            glDrawArrays(GL_TRIANGLES, 0, mesh->vertCount);
#endif
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
            int baseMap = mesh->GetMaterial()->baseMap;
            if (baseMap >= 0 && (size_t)baseMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[baseMap]);
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("base_map_unit"), 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            int normalMap = mesh->GetMaterial()->normalMap;
            if (normalMap >= 0 && (size_t)normalMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[normalMap]);
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("normal_map_unit"), 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            int aoMap = mesh->GetMaterial()->aoMap;
            if (aoMap >= 0 && (size_t)aoMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[aoMap]);
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("ao_map_unit"), 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            int roughnessMap = mesh->GetMaterial()->roughnessMap;
            if (roughnessMap >= 0 && (size_t)roughnessMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[roughnessMap]);
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("roughness_map_unit"), 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            int metallicMap = mesh->GetMaterial()->metallicMap;
            if (metallicMap >= 0 && (size_t)metallicMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[metallicMap]);
            } else if (assetManager.GetDefaultTextures().size() > 4) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[4]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            colorShader->SetUniform(std::string("metallic_map_unit"), 6);

            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);

#ifdef __EMSCRIPTEN__
            // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
            for (const auto& inst : instances) {
                colorShader->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                  mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
            }
#else
            // Upload batched instance data
            if (!instances.empty()) {
                glBufferData(
                  GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                );
                glDrawElementsInstanced(
                  mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
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
    auto [width, height] = Window::Get()->GetFramebufferSize();
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
            int baseMap = mesh->GetMaterial()->baseMap;
            if (baseMap >= 0 && (size_t)baseMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[baseMap]);
            } else if (assetManager.GetDefaultTextures().size() > 0) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[0]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("baseMap", 2);

            // Normal Map (Unit 3)
            glActiveTexture(GL_TEXTURE3);
            int normalMap = mesh->GetMaterial()->normalMap;
            if (normalMap >= 0 && (size_t)normalMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[normalMap]);
            } else if (assetManager.GetDefaultTextures().size() > 1) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[1]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("normalMap", 3);

            // AO Map (Unit 4)
            glActiveTexture(GL_TEXTURE4);
            int aoMap = mesh->GetMaterial()->aoMap;
            if (aoMap >= 0 && (size_t)aoMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[aoMap]);
            } else if (assetManager.GetDefaultTextures().size() > 2) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[2]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("aoMap", 4);

            // Roughness Map (Unit 5)
            glActiveTexture(GL_TEXTURE5);
            int roughnessMap = mesh->GetMaterial()->roughnessMap;
            if (roughnessMap >= 0 && (size_t)roughnessMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[roughnessMap]);
            } else if (assetManager.GetDefaultTextures().size() > 3) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[3]);
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            geometryShader->SetUniform("roughnessMap", 5);

            // Metallic Map (Unit 6)
            glActiveTexture(GL_TEXTURE6);
            int metallicMap = mesh->GetMaterial()->metallicMap;
            if (metallicMap >= 0 && (size_t)metallicMap < assetManager.GetTextures().size()) {
                glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[metallicMap]);
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
                  mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
            }
            glBindVertexArray(0);
        }
    }

    renderer.CheckErrors("Geometry pass");
}

void DeferredLightingPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("DeferredLightingPass");
    auto [width, height] = Window::Get()->GetFramebufferSize();
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
    lightingShader->SetUniform("pointLightCount", (int)ctx->pointLights.size());

    renderer.screenBuffer->Draw(enc, PrimitiveTopology::TriangleStrip);

    renderer.CheckErrors("Lighting pass");
}

void TransparentPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("TransparentPass");
    auto& cam = *ctx->GetMainCamera();
    Atmospheric::CameraInfo camInfo = { .view = cam.GetViewMatrix(),
                                        .projection = cam.GetProjectionMatrix(),
                                        .position = cam.GetEyePosition() };
    Atmospheric::ParticleServer::GetInstance().Draw(camInfo);// TODO: transparent pass
}

void MSAAResolvePass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("MSAAResolvePass");
    
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);
 
    // Resolve MSAA color + depth — both RTs now use GL_DEPTH_COMPONENT32F.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
 
    renderer.CheckErrors("MSAA resolve pass");
}

// WorldCanvasPass: World sprites with depth testing
void WorldCanvasPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
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

    auto [width, height] = Window::Get()->GetFramebufferSize();
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
#ifndef __EMSCRIPTEN__
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

        if (renderer.GetCanvasQueue().empty()) return;

        auto [width, height] = Window::Get()->GetFramebufferSize();
        const glm::mat4 viewProj = glm::ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

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

    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifndef __EMSCRIPTEN__
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
        auto [winW, winH] = Window::Get()->GetSize();
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

// Helper to reduce code duplication


void PostProcessPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
    ZoneScopedN("PostProcessPass");

    auto size = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, size.width, size.height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
    ZoneScopedN("UIPass");
    auto* batchRenderer = renderer.GetBatchRenderer();
    auto& queue = renderer.GetUIQueue();

    // Setup OpenGL state for UI
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto [winW, winH] = Window::Get()->GetSize();
    glm::mat4 projection = glm::ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);

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

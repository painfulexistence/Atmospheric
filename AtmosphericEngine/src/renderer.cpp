#include "renderer.hpp"
#include "asset_manager.hpp"
#include "console.hpp"
#include "graphics_server.hpp"
#include "window.hpp"
#include <algorithm>

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

void RenderPipeline::AddPass(std::unique_ptr<RenderPass> pass) {
    _passes.push_back(std::move(pass));
}

void RenderPipeline::Render(GraphicsServer* ctx, Renderer& renderer) {
    // Execute all passes in order (sorting, batching, drawing)
    for (auto& pass : _passes) {
        pass->Execute(ctx, renderer);
    }
}

void Renderer::Init(int width, int height) {
    CreateFBOs();
    CreateRTs(RenderTargetProps{ width, height });
    CreateDebugVAO();
    CreateCanvasVAO();
    CreateScreenVAO();

    _pipeline = std::make_unique<RenderPipeline>();
    _pipeline->AddPass(std::make_unique<ShadowPass>());
    _pipeline->AddPass(std::make_unique<ForwardOpaquePass>());
    _pipeline->AddPass(std::make_unique<MSAAResolvePass>());
    _pipeline->AddPass(std::make_unique<CanvasPass>());
    _pipeline->AddPass(std::make_unique<PostProcessPass>());
}

void Renderer::Cleanup() {
    DestroyRTs();
    DestroyFBOs();

    glDeleteVertexArrays(1, &debugVAO);
    glDeleteVertexArrays(1, &canvasVAO);
    glDeleteVertexArrays(1, &screenVAO);
    glDeleteBuffers(1, &debugVBO);
    glDeleteBuffers(1, &canvasVBO);
    glDeleteBuffers(1, &screenVBO);
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
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0); // Unbind
    glDisable(GL_RASTERIZER_DISCARD);
}

void Renderer::SortAndBucket(const glm::vec3& cameraPos) {
    // Clear previous frame's sorted queues
    _opaqueQueue.clear();
    _transparentQueue.clear();
    _hudQueue.clear();
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
            _hudQueue.push_back(sortable);
        }
    }
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


void Renderer::RenderFrame(GraphicsServer* ctx, float dt) {
    SortAndBucket(ctx->GetMainCamera()->GetEyePosition());
    _pipeline->Render(ctx, *this);
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
    glGenFramebuffers(1, &sceneFBO);
    glGenFramebuffers(1, &msaaResolveFBO);
    glGenFramebuffers(1, &gBuffer.id);
}

void Renderer::DestroyFBOs() {
    glDeleteFramebuffers(1, &shadowFBO);
    glDeleteFramebuffers(1, &sceneFBO);
    glDeleteFramebuffers(1, &msaaResolveFBO);
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
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
              GL_DEPTH_COMPONENT,
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

#ifndef __EMSCRIPTEN__
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

    // 2. Create and set HDR pass attachments
    glGenTextures(1, &sceneColorTexture);
#ifdef __EMSCRIPTEN__
    glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
#else
    if (_currRenderPath == RenderPath::Forward) {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, sceneColorTexture);
        glTexImage2DMultisample(
          GL_TEXTURE_2D_MULTISAMPLE, props.numSamples, GL_RGBA16F, props.width, props.height, GL_TRUE
        );
    } else {
        glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);
    }
#endif
    if (glIsTexture(sceneColorTexture) != GL_TRUE) {
        throw std::runtime_error("Failed to create HDR color texture");
    }

    glGenTextures(1, &sceneDepthTexture);
#ifdef __EMSCRIPTEN__
    glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
    );
#else
    if (_currRenderPath == RenderPath::Forward) {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, sceneDepthTexture);
        glTexImage2DMultisample(
          GL_TEXTURE_2D_MULTISAMPLE, props.numSamples, GL_DEPTH_COMPONENT, props.width, props.height, GL_TRUE
        );
    } else {
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        glTexImage2D(
          GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
        );
    }
#endif
    if (glIsTexture(sceneDepthTexture) != GL_TRUE) {
        throw std::runtime_error("Failed to create HDR depth texture");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
#ifdef __EMSCRIPTEN__
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTexture, 0);
#else
    if (_currRenderPath == RenderPath::Forward) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, sceneColorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, sceneDepthTexture, 0);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTexture, 0);
    }
#endif
    CheckFramebufferStatus("HDR framebuffer incomplete");
    CheckErrors("Create HDR RTs");

    // 3. Create and set MSAA resolve pass attachments
    glGenTextures(1, &msaaResolveTexture);
    glBindTexture(GL_TEXTURE_2D, msaaResolveTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, msaaResolveFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, msaaResolveTexture, 0);
    CheckFramebufferStatus("MSAA framebuffer incomplete");
    CheckErrors("Create MSAA resolve RTs");

    // 4. Create and set geometry pass attachments
    glGenTextures(1, &gBuffer.positionRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.positionRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, props.width, props.height, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.normalRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.normalRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, props.width, props.height, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.albedoRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.albedoRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.materialRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.materialRT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, props.width, props.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &gBuffer.depthRT);
    glBindTexture(GL_TEXTURE_2D, gBuffer.depthRT);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL
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
    glDeleteTextures(1, &sceneColorTexture);
    glDeleteTextures(1, &sceneDepthTexture);
    glDeleteTextures(1, &msaaResolveTexture);

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
    glVertexAttribPointer(3, 1, GL_INT, GL_FALSE, sizeof(CanvasVertex), (void*)offsetof(CanvasVertex, texIndex));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

void Renderer::CreateScreenVAO() {
    std::array<ScreenVertex, 4> verts = { {
      { { -1.0f, 1.0f }, { 0.0f, 1.0f } },
      { { -1.0f, -1.0f }, { 0.0f, 0.0f } },
      { { 1.0f, 1.0f }, { 1.0f, 1.0f } },
      { { 1.0f, -1.0f }, { 1.0f, 0.0f } },
    } };
    glGenVertexArrays(1, &screenVAO);
    glGenBuffers(1, &screenVBO);

    glBindVertexArray(screenVAO);
    glBindBuffer(GL_ARRAY_BUFFER, screenVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ScreenVertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), (void*)(offsetof(ScreenVertex, texCoord)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::CreateDebugVAO() {
    glGenVertexArrays(1, &debugVAO);
    glGenBuffers(1, &debugVBO);

    glBindVertexArray(debugVAO);
    glBindBuffer(GL_ARRAY_BUFFER, debugVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)offsetof(DebugVertex, color));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ShadowPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
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

            // Upload ALL instances for this batch once
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW);

            // Instanced Draw
            glDrawElementsInstanced(
              mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
            );
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

                    // Upload ALL instances for this batch once
                    glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
                    glBufferData(
                      GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                    );

                    // Instanced Draw
                    glDrawElementsInstanced(
                      mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                    );
                }
                glBindVertexArray(0);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ForwardOpaquePass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.sceneFBO);
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
    for (int i = 0; i < assetManager.GetDefaultTextures().size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + DEFAULT_TEXTURE_BASE_INDEX + i);
        glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[i]);
    }
    for (int i = 0; i < assetManager.GetTextures().size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + SCENE_TEXTURE_BASE_INDEX + i);
        glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[i]);
    }

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

        // glEnable(GL_STENCIL_TEST);
        // glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        // glStencilFunc(GL_ALWAYS, 1, 0xFF);

        // Outline rendering
        // glStencilMask(0xFF);
        // glStencilFunc(GL_ALWAYS, 1, 0xFF);
        // /*
        // pass 1
        // ...
        //  */
        // glStencilMask(0x00);
        // glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        // glDepthFunc(GL_ALWAYS);
        // /*
        // pass 2 (scaled)
        // ...
        //  */
        // glDepthFunc(GL_LESS);

        // glStencilMask(0xFF);
        // glStencilFunc(GL_ALWAYS, 1, 0xFF);

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

        // glEnable(GL_PRIMITIVE_RESTART);

        switch (mesh->type) {

        case MeshType::TERRAIN:
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
            terrainShader->SetUniform(
              std::string("height_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->heightMap
            );
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

        case MeshType::SKY:
            // TODO: implement skybox rendering
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

            // Material textures
            if (mesh->GetMaterial()->baseMap >= 0) {
                colorShader->SetUniform(
                  std::string("base_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->baseMap
                );
            } else {
                colorShader->SetUniform(std::string("base_map_unit"), DEFAULT_TEXTURE_BASE_INDEX + 0);
            }
            if (mesh->GetMaterial()->normalMap >= 0) {
                colorShader->SetUniform(
                  std::string("normal_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->normalMap
                );
            } else {
                colorShader->SetUniform(std::string("normal_map_unit"), DEFAULT_TEXTURE_BASE_INDEX + 1);
            }
            if (mesh->GetMaterial()->aoMap >= 0) {
                colorShader->SetUniform(
                  std::string("ao_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->aoMap
                );
            } else {
                colorShader->SetUniform(std::string("ao_map_unit"), DEFAULT_TEXTURE_BASE_INDEX + 2);
            }
            if (mesh->GetMaterial()->roughnessMap >= 0) {
                colorShader->SetUniform(
                  std::string("roughness_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->roughnessMap
                );
            } else {
                colorShader->SetUniform(std::string("roughness_map_unit"), DEFAULT_TEXTURE_BASE_INDEX + 3);
            }
            if (mesh->GetMaterial()->metallicMap >= 0) {
                colorShader->SetUniform(
                  std::string("metallic_map_unit"), SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->metallicMap
                );
            } else {
                colorShader->SetUniform(std::string("metallic_map_unit"), DEFAULT_TEXTURE_BASE_INDEX + 4);
            }

            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);

            // Upload batched instance data
            if (!instances.empty()) {
                glBufferData(
                  GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW
                );
                glDrawElementsInstanced(
                  mesh->GetMaterial()->primitiveType, mesh->triCount * 3, GL_UNSIGNED_SHORT, 0, instances.size()
                );
            }

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

        glBindVertexArray(renderer.debugVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.debugVBO);
        glBufferData(
          GL_ARRAY_BUFFER, ctx->debugLines.size() * sizeof(DebugVertex), ctx->debugLines.data(), GL_DYNAMIC_DRAW
        );
        glDrawArrays(GL_LINES, 0, ctx->debugLines.size());
        glBindVertexArray(0);

        // glEnable(GL_DEPTH_TEST);

        ctx->debugLines.clear();
        ctx->_debugLineCount = 0;
    }

    renderer.CheckErrors("Opaque pass");
}

void DeferredGeometryPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gBuffer.id);
    std::array<GLuint, 4> attachments = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(attachments.size(), attachments.data());
    // for (int i = 0; i < MAX_UNI_LIGHTS; ++i) {
    //     glActiveTexture(GL_TEXTURE0 + i);
    //     glBindTexture(GL_TEXTURE_2D, uniShadowMaps[i]);
    // }
    // for (int i = 0; i < MAX_OMNI_LIGHTS; ++i) {
    //     glActiveTexture(GL_TEXTURE0 + UNI_SHADOW_MAP_COUNT + i);
    //     glBindTexture(GL_TEXTURE_CUBE_MAP, omniShadowMaps[i]);
    // }
    auto& assetManager = AssetManager::Get();
    for (int i = 0; i < assetManager.GetDefaultTextures().size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + DEFAULT_TEXTURE_BASE_INDEX + i);
        glBindTexture(GL_TEXTURE_2D, assetManager.GetDefaultTextures()[i]);
    }
    for (int i = 0; i < assetManager.GetTextures().size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + SCENE_TEXTURE_BASE_INDEX + i);
        glBindTexture(GL_TEXTURE_2D, assetManager.GetTextures()[i]);
    }

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
        case MeshType::PRIM:
        default:
            if (mesh->GetMaterial()->baseMap >= 0) {
                geometryShader->SetUniform("baseMap", SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->baseMap);
            } else {
                geometryShader->SetUniform("baseMap", DEFAULT_TEXTURE_BASE_INDEX + 0);
            }
            if (mesh->GetMaterial()->normalMap >= 0) {
                geometryShader->SetUniform("normalMap", SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->normalMap);
            } else {
                geometryShader->SetUniform("normalMap", DEFAULT_TEXTURE_BASE_INDEX + 1);
            }
            if (mesh->GetMaterial()->aoMap >= 0) {
                geometryShader->SetUniform("aoMap", SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->aoMap);
            } else {
                geometryShader->SetUniform("aoMap", DEFAULT_TEXTURE_BASE_INDEX + 2);
            }
            if (mesh->GetMaterial()->roughnessMap >= 0) {
                geometryShader->SetUniform(
                  "roughnessMap", SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->roughnessMap
                );
            } else {
                geometryShader->SetUniform("roughnessMap", DEFAULT_TEXTURE_BASE_INDEX + 3);
            }
            if (mesh->GetMaterial()->metallicMap >= 0) {
                geometryShader->SetUniform("metallicMap", SCENE_TEXTURE_BASE_INDEX + mesh->GetMaterial()->metallicMap);
            } else {
                geometryShader->SetUniform("metallicMap", DEFAULT_TEXTURE_BASE_INDEX + 4);
            }

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

void DeferredLightingPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.sceneFBO);

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

    glBindVertexArray(renderer.screenVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    renderer.CheckErrors("Lighting pass");
}

void TransparentPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    auto& cam = *ctx->GetMainCamera();
    Atmospheric::CameraInfo camInfo = { .view = cam.GetViewMatrix(), .projection = cam.GetProjectionMatrix(), .position = cam.GetEyePosition() };
    Atmospheric::ParticleServer::GetInstance().Draw(camInfo);    // TODO: transparent pass
}

void MSAAResolvePass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    auto [width, height] = Window::Get()->GetFramebufferSize();
    glViewport(0, 0, width, height);

    // Resolve MSAA
    glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer.sceneFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.postProcessEnabled ? renderer.msaaResolveFBO : renderer.finalFBO);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    renderer.CheckErrors("MSAA resolve pass");
}

void CanvasPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    ctx->_canvasQuadCount = ctx->canvasDrawList.size() / 6;
    if (ctx->canvasDrawList.size() > 0) {
        auto [width, height] = Window::Get()->GetFramebufferSize();

        glViewport(0, 0, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer.postProcessEnabled ? renderer.msaaResolveFBO : renderer.finalFBO);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifndef __EMSCRIPTEN__
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

        // TODO: use canvas textures
        for (int i = 0; i < AssetManager::Get().GetTextures().size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, AssetManager::Get().GetTextures()[i]);
        }
        // for (int i = 0; i < MAX_CANVAS_TEXTURES; ++i) {
        //     glActiveTexture(GL_TEXTURE0 + i);
        //     if (i < ctx->canvasTextures.size()) {
        //         glBindTexture(GL_TEXTURE_2D, ctx->canvasTextures[i]);
        //     } else {
        //         glBindTexture(GL_TEXTURE_2D, 0);
        //     }
        // }

        auto canvasShader = ctx->GetShader("canvas");
        canvasShader->Activate();
        glm::mat4 projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);
        canvasShader->SetUniform(std::string("Projection"), projection);
        for (int i = 0; i < MAX_CANVAS_TEXTURES; i++) {
            canvasShader->SetUniform(fmt::format("Textures[{}]", i), i);
        }

        glBindVertexArray(renderer.canvasVAO);
        glBindBuffer(GL_ARRAY_BUFFER, renderer.canvasVBO);
        glBufferData(
          GL_ARRAY_BUFFER,
          ctx->canvasDrawList.size() * sizeof(CanvasVertex),
          ctx->canvasDrawList.data(),
          GL_DYNAMIC_DRAW
        );
        glDrawArrays(GL_TRIANGLES, 0, ctx->canvasDrawList.size());
        glBindVertexArray(0);
        ctx->canvasDrawList.clear();

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
}

void PostProcessPass::Execute(GraphicsServer* ctx, Renderer& renderer) {
    if (!renderer.postProcessEnabled) {
        return;
    }

    auto size = Window::Get()->GetFramebufferSize();

    glViewport(0, 0, size.width, size.height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // glDisable(GL_DEPTH_TEST);
    // glDisable(GL_BLEND);
#ifndef __EMSCRIPTEN__
    // glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.msaaResolveTexture);

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auto postProcessShader = ctx->GetShader("hdr");
    postProcessShader->Activate();
    postProcessShader->SetUniform(std::string("color_map_unit"), (int)0);
    glBindVertexArray(renderer.screenVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    renderer.CheckErrors("Post process pass");
}

#include "renderer.hpp"
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
#include "vat.hpp"
#include "window.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "command_encoder.hpp"
#include "gpu_canvas_pass.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "renderer_wgsl.hpp"
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
    case PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::Lines:
        return GL_LINES;
    case PrimitiveTopology::LineStrip:
        return GL_LINE_STRIP;
    case PrimitiveTopology::Points:
        return GL_POINTS;
    }
    return GL_TRIANGLES;
}

// The material a command actually draws with: its own override when set,
// otherwise the mesh's material (the historical coupling). Centralized so the
// sort key, batch key, and every draw loop agree on one answer.
static MaterialHandle EffectiveMaterial(const RenderCommand& cmd) {
    if (cmd.material.IsValid()) return cmd.material;
    Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
    return mesh ? mesh->GetMaterial() : MaterialHandle{};
}

struct RenderBatch {
    MeshHandle mesh;
    MaterialHandle material;// effective material shared by every instance
    std::vector<InstanceData> instances;
};

// Merges consecutive commands sharing the same (mesh, material) into one
// instanced batch. The queue is pre-sorted, so equal keys are adjacent. A
// command carrying an instance span (a MeshInstancer's whole cloud) contributes
// all of its instances at once; a plain command contributes its lone transform.
// Scattered MeshRenderers and an instancer of the same mesh+material fold into
// the same batch for free.
static std::vector<RenderBatch> BuildBatches(const std::vector<Renderer::SortableCommand>& queue) {
    std::vector<RenderBatch> batches;
    RenderBatch currentBatch;
    bool haveCurrent = false;

    for (const auto& sortable : queue) {
        const auto& cmd = sortable.cmd;
        MaterialHandle effMat = EffectiveMaterial(cmd);

        if (!haveCurrent || currentBatch.mesh != cmd.mesh || currentBatch.material != effMat) {
            if (haveCurrent && !currentBatch.instances.empty()) {
                batches.push_back(std::move(currentBatch));
            }
            currentBatch = RenderBatch{};
            currentBatch.mesh = cmd.mesh;
            currentBatch.material = effMat;
            haveCurrent = true;
        }

        if (cmd.instances && cmd.instanceCount > 0) {
            currentBatch.instances.insert(
                currentBatch.instances.end(), cmd.instances, cmd.instances + cmd.instanceCount
            );
        } else {
            currentBatch.instances.push_back(InstanceData{ cmd.transform });
        }
    }

    if (haveCurrent && !currentBatch.instances.empty()) {
        batches.push_back(std::move(currentBatch));
    }

    return batches;
}

static constexpr int gmaxCanvasTextures = 32;

// Strips the leading digit-length prefix (GCC mangled names) and
// "class "/"struct " prefix (MSVC) from a typeid name string.
static std::string CleanTypeName(const char* raw) {
    while (*raw && std::isdigit(static_cast<unsigned char>(*raw)))
        ++raw;
    for (const char* prefix : { "class ", "struct " }) {
        if (std::strncmp(raw, prefix, std::strlen(prefix)) == 0) {
            raw += std::strlen(prefix);
            break;
        }
    }
    return raw;
}

RenderGraph::~RenderGraph() {
    for (auto& e : _entries)
        e.timer.Destroy();
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
        out.push_back({ e.name, e.timer.GetMs() });
        total += e.timer.GetMs();
    }
    out.push_back({ "[Total]", total });
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
    // GL-only: no GL context exists when running the WebGPU backend.
    if (GfxFactory::GetBackend() != GfxBackend::WebGPU) {
        static const float gquadVerts[] = {
            -1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f,  1.f, 1.f, 1.f,
            -1.f, -1.f, 0.f, 0.f, 1.f, 1.f,  1.f, 1.f, -1.f, 1.f, 0.f, 1.f,
        };
        GLuint vbo;
        glGenVertexArrays(1, &gl.screenQuadVAO);
        glGenBuffers(1, &vbo);
        glBindVertexArray(gl.screenQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(gquadVerts), gquadVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);

        // ── Skybox cube VAO ───────────────────────────────────────────
        static const float gcubeVerts[] = {
            -1, -1, -1, 1,  -1, -1, 1,  1,  -1, 1,  1,  -1, -1, 1,  -1, -1, -1, -1, -1, -1, 1,  1,  -1, 1,  1,  1,  1,
            1,  1,  1,  -1, 1,  1,  -1, -1, 1,  -1, 1,  1,  -1, 1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  -1, 1,  1,
            1,  1,  1,  1,  1,  -1, 1,  -1, -1, 1,  -1, -1, 1,  -1, 1,  1,  1,  1,  -1, -1, -1, -1, -1, 1,  1,  -1, 1,
            1,  -1, 1,  1,  -1, -1, -1, -1, -1, -1, 1,  -1, -1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  -1, -1, 1,  -1,
        };
        glGenVertexArrays(1, &gl.skyboxVAO);
        glGenBuffers(1, &gl.skyboxVBO);
        glBindVertexArray(gl.skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gl.skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(gcubeVerts), gcubeVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }

    _renderGraph = std::make_unique<RenderGraph>();
    _renderGraph->AddPass(std::make_unique<ShadowPass>());
    _renderGraph->AddPass(std::make_unique<PlanarReflectionPass>());// mirrored world RT for WaterPass
    _renderGraph->AddPass(std::make_unique<PortalPass>());// recursive portal-view RTs
    _renderGraph->AddPass(std::make_unique<ForwardOpaquePass>());
    _renderGraph->AddPass(std::make_unique<SkyboxPass>());// after clear, fills empty sky pixels
    _renderGraph->AddPass(std::make_unique<SunPass>());
    _renderGraph->AddPass(std::make_unique<VoxelChunkPass>());
    _renderGraph->AddPass(
        std::make_unique<MicroVoxelPass>()
    );// raymarched micro voxels (renders registered VoxelVolumeComponents)
    _renderGraph->AddPass(std::make_unique<PortalSurfacePass>());// portal windows in the main view
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

std::vector<glm::mat4> Renderer::GetAuxViewProjs() {
    // Last frame's aux views (submission runs before the passes). Portal
    // recursion levels plus the water reflection, so the submission side can
    // union-cull against them instead of disabling culling entirely.
    std::vector<glm::mat4> out;
    if (auto* portals = GetPass<PortalPass>()) {
        const auto& vps = portals->ActiveViewProjs();
        out.insert(out.end(), vps.begin(), vps.end());
    }
    if (auto* refl = GetPass<PlanarReflectionPass>()) {
        if (refl->IsActive()) out.push_back(refl->GetReflectionViewProj());
    }
    return out;
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
    static Material gfallback{ MaterialProps{} };
    return &gfallback;
}

// Binds a VAT clip's position texture (unit 8) and playback uniforms onto a
// depth/shadow shader, so a VAT mesh casts a shadow matching its animation.
// Falls back to vat_enabled=0 (rest pose) when the clip isn't ready. The forward
// pass binds VAT state inline (it also needs the normal texture); this helper
// covers the position-only depth shaders (vat_depth / vat_depth_cubemap).
static void BindVATDepthUniforms(ShaderProgram* shader, VATMaterial* vatMat) {
    VATClip* clip = vatMat ? vatMat->clip : nullptr;
    bool ready = clip && clip->GetVertCount() > 0 && clip->GetFrameCount() > 0;
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, ready ? clip->GetPositionTexture() : 0);
    shader->SetUniform(std::string("vat_position_map"), 8);
    shader->SetUniform(std::string("vat_enabled"), ready ? 1 : 0);
    shader->SetUniform(std::string("vat_vert_count"), ready ? static_cast<int>(clip->GetVertCount()) : 0);
    shader->SetUniform(std::string("vat_frame_count"), ready ? static_cast<int>(clip->GetFrameCount()) : 0);
    shader->SetUniform(std::string("vat_time"), vatMat ? vatMat->normalizedTime : 0.0f);
}

uint64_t Renderer::CalculateSortKey(const RenderCommand& cmd, const glm::vec3& cameraPos) {
    MaterialHandle effMat = EffectiveMaterial(cmd);
    Material* mat = AssetManager::Get().ResolveMaterial(effMat);
    if (!mat) return 0;

    // Calculate depth (distance from camera)
    auto objPos = glm::vec3(cmd.transform[3]);
    float depth = glm::length(objPos - cameraPos);

    // Get render queue
    int renderQueue = mat->GetFinalRenderQueue();

    // Get material and mesh IDs for batching; handle IDs are stable across
    // runs, unlike the pointer bits used before. Material is the *effective*
    // one (override or mesh's), so overridden draws sort next to their true
    // material and batch together.
    uint32_t materialID = effMat.id & 0xFFFF;
    uint32_t meshID = cmd.mesh.id & 0xFFFF;

    // Generate 64-bit sort key
    // [16 bits: render queue] [16 bits: depth] [16 bits: material] [16 bits: mesh]
    uint64_t key = 0;
    key |= static_cast<uint64_t>(renderQueue & 0xFFFF) << 48;
    // Clamp before converting: float-to-uint16 is undefined past 65535, and
    // open-world depths exceed 655m routinely. Distant draws saturate the
    // depth bits and fall back to material/mesh ordering, which is fine.
    key |= static_cast<uint64_t>(static_cast<uint16_t>(std::min(depth * 100.0f, 65535.0f)) & 0xFFFF) << 32;
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
        _transparentQueue.begin(), _transparentQueue.end(), [](const SortableCommand& a, const SortableCommand& b) {
            return a.sortKey > b.sortKey;
        }
    );
}

void Renderer::BucketCommands(const glm::vec3& cameraPos) {
    for (const auto& cmd : _commandList) {
        Material* mat = AssetManager::Get().ResolveMaterial(EffectiveMaterial(cmd));
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
        ConsoleSubsystem::Get()->Error(fmt::format("{}: {}\n", prefix, error));
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
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_W, SHADOW_H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
            );
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
                    nullptr
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
        for (int i = 0; i < static_cast<int>(gl.uniShadowMaps.size()); ++i) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.uniShadowMaps[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }
        for (int i = 0; i < static_cast<int>(gl.omniShadowMaps.size()); ++i) {
            glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, gl.omniShadowMaps[i], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }
#endif
        CheckErrors("Create shadow RTs");
    }// isGL

    // 2. Scene render target (MSAA on forward path; RBO-based on WebGL, texture-based on desktop)
    {
        RenderTarget::Props p;
        p.width = props.width;
        p.height = props.height;
        p.withDepth = true;
        p.hdr = true;
        if (_currRenderPath == RenderPath::Forward) p.numSamples = props.numSamples;
        sceneRT = GfxFactory::CreateRenderTarget(p);
    }

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (isGL && sceneRT && sceneRT->GetNumSamples() > 1) {
        glGenTextures(1, &gl.glesResolvedDepthTex);
        glBindTexture(GL_TEXTURE_2D, gl.glesResolvedDepthTex);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
        );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &gl.glesResolvedDepthFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, gl.glesResolvedDepthFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.glesResolvedDepthTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            ConsoleSubsystem::Get()->Error("Renderer: WebGL resolved depth FBO incomplete!");
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &gl.gBuffer.normalRT);
        glBindTexture(GL_TEXTURE_2D, gl.gBuffer.normalRT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, props.width, props.height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &gl.gBuffer.albedoRT);
        glBindTexture(GL_TEXTURE_2D, gl.gBuffer.albedoRT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &gl.gBuffer.materialRT);
        glBindTexture(GL_TEXTURE_2D, gl.gBuffer.materialRT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, props.width, props.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &gl.gBuffer.depthRT);
        glBindTexture(GL_TEXTURE_2D, gl.gBuffer.depthRT);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, props.width, props.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
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
    }// isGL
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), nullptr);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), reinterpret_cast<void*>(offsetof(CanvasVertex, texCoord))
    );
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), reinterpret_cast<void*>(offsetof(CanvasVertex, color))
    );
    glVertexAttribIPointer(
        3, 1, GL_INT, sizeof(CanvasVertex), reinterpret_cast<void*>(offsetof(CanvasVertex, texIndex))
    );
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

void Renderer::CreateScreenBuffer() {
    std::array<ScreenVertex, 4> verts = { {
        { { -1.0f, 1.0f }, { 0.0f, 1.0f } },
        { { -1.0f, -1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 1.0f, -1.0f }, { 1.0f, 0.0f } },
    } };
    screenBuffer = GfxFactory::CreateBuffer();
    screenBuffer->Initialize(VertexFormat::Screen, BufferUsage::Static);
    screenBuffer->Upload(verts.data(), verts.size(), sizeof(ScreenVertex));
}

void Renderer::CreateDebugBuffer() {
    debugBuffer = GfxFactory::CreateBuffer();
    debugBuffer->Initialize(VertexFormat::Debug, BufferUsage::Stream);
}

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
ShadowPass::~ShadowPass() {
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    for (auto& [id, entry] : _vatBGCache)
        wgpuBindGroupRelease(entry.bg);
    if (_vatUniformBG) wgpuBindGroupRelease(_vatUniformBG);
    if (_vatUniformBGL) wgpuBindGroupLayoutRelease(_vatUniformBGL);
    if (_vatTexBGL) wgpuBindGroupLayoutRelease(_vatTexBGL);
    if (_vatPipeline) wgpuRenderPipelineRelease(_vatPipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf) wgpuBufferRelease(_drawUniformBuf);
    if (_shadowView) wgpuTextureViewRelease(_shadowView);
    if (_shadowTex) wgpuTextureRelease(_shadowTex);
}

void ShadowPass::_initGPU(WGPUDevice device, WGPUQueue queue) {
    _gpuDevice = device;
    _gpuQueue = queue;

    {
        WGPUBufferDescriptor d{};
        d.size = SHADOW_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    {
        WGPUTextureDescriptor d{};
        d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        d.dimension = WGPUTextureDimension_2D;
        d.size = { SHADOW_W, SHADOW_H, 1 };
        d.format = WGPUTextureFormat_Depth32Float;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        _shadowTex = wgpuDeviceCreateTexture(device, &d);
        _shadowView = wgpuTextureCreateView(_shadowTex, nullptr);
    }

    // Vertex layout: only position (offset 0) from the 56-byte Standard format.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(SHADOW_WGSL)
                 .bgl(
                     { gpuUniform(0, wgsl_stage::vert, SHADOW_FRAME_UNIFORM_SIZE),
                       gpuDynUniform(1, wgsl_stage::vert, SHADOW_DRAW_UNIFORM_SIZE) }
                 )
                 .vertex(56, { { WGPUVertexFormat_Float32x3, 0, 0 } })
                 .depth(true, WGPUCompareFunction_Less)
                 .depthOnly()
                 // Front-face culling like classic shadow mapping would change peter-
                 // panning behaviour vs the GL path (which draws per-material culling);
                 // keep None to match GL output most closely.
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);

    // VAT caster pipeline: same depth-only target, wider draw slot (adds
    // vatParams) and a group 1 for the animation's position texture.
    auto vp = GpuPipelineBuilder(device)
                  .wgsl(SHADOW_VAT_WGSL)
                  .bgl(
                      { gpuUniform(0, wgsl_stage::vert, SHADOW_FRAME_UNIFORM_SIZE),
                        gpuDynUniform(1, wgsl_stage::vert, SHADOW_VAT_DRAW_UNIFORM_SIZE) }
                  )
                  .bgl({ gpuUnfilterableTexture(0, wgsl_stage::vert) })
                  .vertex(56, { { WGPUVertexFormat_Float32x3, 0, 0 } })
                  .depth(true, WGPUCompareFunction_Less)
                  .depthOnly()
                  .build();
    _vatPipeline = vp.pipeline;
    _vatUniformBGL = vp.bgl(0);
    _vatTexBGL = vp.bgl(1);
}

WGPUBindGroup ShadowPass::_getOrCreateVATBG(uint32_t posTexID) {
    WGPUTexture posTex = GfxFactory::GetWGPUTexture(posTexID);
    if (!posTex) return nullptr;
    auto it = _vatBGCache.find(posTexID);
    if (it != _vatBGCache.end()) {
        if (it->second.posTex == posTex) return it->second.bg;
        wgpuBindGroupRelease(it->second.bg);
        _vatBGCache.erase(it);
    }
    WGPUBindGroup bg = GpuBindGroupBuilder(_gpuDevice, _vatTexBGL).texture(0, posTex).build();
    _vatBGCache[posTexID] = { bg, posTex };
    return bg;
}

void ShadowPass::_ensureDrawCapacity(uint32_t drawCount) {
    if (drawCount <= _drawSlotCapacity && _drawUniformBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(drawCount, std::max<uint32_t>(_drawSlotCapacity * 2, 16));

    if (_drawUniformBuf) {
        wgpuBufferRelease(_drawUniformBuf);
        _drawUniformBuf = nullptr;
    }
    if (_uniformBG) {
        wgpuBindGroupRelease(_uniformBG);
        _uniformBG = nullptr;
    }
    if (_vatUniformBG) {
        wgpuBindGroupRelease(_vatUniformBG);
        _vatUniformBG = nullptr;
    }

    WGPUBufferDescriptor d{};
    d.size = static_cast<uint64_t>(newCapacity) * SHADOW_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
                     .buffer(0, _frameUniformBuf, SHADOW_FRAME_UNIFORM_SIZE)
                     .buffer(1, _drawUniformBuf, SHADOW_DRAW_UNIFORM_SIZE)
                     .build();
    // VAT casters read the same buffer through a wider (model + vatParams) draw
    // binding; slots are 256-byte strided so the extra 16 bytes fit.
    _vatUniformBG = GpuBindGroupBuilder(_gpuDevice, _vatUniformBGL)
                        .buffer(0, _frameUniformBuf, SHADOW_FRAME_UNIFORM_SIZE)
                        .buffer(1, _drawUniformBuf, SHADOW_VAT_DRAW_UNIFORM_SIZE)
                        .build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void ShadowPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        WGPUDevice dev = GfxFactory::GetWebGPUDevice();
        if (!dev) return;
        if (!_pipeline) _initGPU(dev, GfxFactory::GetWebGPUQueue());

        // Collect casters: PRIM meshes from both queues, same as the GL path.
        // vatMat is non-null (with a ready clip) for VAT casters, which use the
        // displacing depth pipeline so their shadow matches the animation.
        struct DrawItem {
            Buffer* buf;
            glm::mat4 model;
            VATMaterial* vatMat;
        };
        std::vector<DrawItem> draws;
        auto collect = [&](const std::vector<Renderer::SortableCommand>& queue) {
            for (const auto& sortable : queue) {
                const auto& cmd = sortable.cmd;
                Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
                if (!mesh || mesh->type != MeshType::PRIM) continue;
                if (!mesh->UsesRenderMesh()) continue;
                Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
                if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
                VATMaterial* vatMat = dynamic_cast<VATMaterial*>(ResolveMaterialOrFallback(EffectiveMaterial(cmd)));
                // Instance spans have no HW-instanced WebGPU path yet, so a
                // cloud casts shadow as one draw per blade/instance (correct,
                // just not batched). Plain draws keep their single transform.
                if (cmd.instances && cmd.instanceCount > 0) {
                    for (uint32_t k = 0; k < cmd.instanceCount; ++k)
                        draws.push_back({ buf, cmd.instances[k].modelMatrix, vatMat });
                } else {
                    draws.push_back({ buf, cmd.transform, vatMat });
                }
            }
        };
        collect(renderer.GetOpaqueQueue());
        collect(renderer.GetTransparentQueue());

        // Light-space VP with the GL [-1,1] → WebGPU [0,1] NDC-z fixup baked
        // in; without it the ortho projection would clip half the depth range.
        // castShadow gates shadowing exactly like the GL path (pbr.frag's
        // main_light.cast_shadow uniform) — the engine's default light has
        // castShadow=false, so scenes without an explicit shadow light must
        // render fully lit, not self-shadowed.
        auto* light = ctx->GetMainLight();
        const bool shadowsOn = light && light->castShadow;
        glm::mat4 lightVP;
        if (shadowsOn) {
            lightVP = GpuProjectionZ01() * light->GetProjectionMatrix(0) * light->GetViewMatrix();
        } else {
            // Shadowing off: push everything past far so the WGSL guard reads
            // "outside the frustum" → fully lit.
            lightVP = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f))
                      * glm::scale(glm::mat4(1.0f), glm::vec3(0.0f));
            draws.clear();// nothing to render into the map
        }
        renderer.wgpuShadowLightVP = lightVP;
        renderer.wgpuShadowMapView = _shadowView;

        if (!draws.empty()) {
            _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));
            wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &lightVP, sizeof(lightVP));
            std::vector<uint8_t> drawData(draws.size() * SHADOW_DRAW_SLOT_STRIDE, 0);
            for (size_t i = 0; i < draws.size(); ++i) {
                uint8_t* slot = drawData.data() + i * SHADOW_DRAW_SLOT_STRIDE;
                std::memcpy(slot, &draws[i].model, sizeof(glm::mat4));
                // vatParams (model + 64); harmless for non-VAT casters, which are
                // drawn with the plain pipeline that never reads past model.
                VATMaterial* vm = draws[i].vatMat;
                VATClip* clip = vm ? vm->clip : nullptr;
                bool ready = clip && clip->GetVertCount() > 0 && clip->GetFrameCount() > 0;
                glm::vec4 vatParams(
                    ready ? static_cast<float>(clip->GetVertCount()) : 0.0f,
                    ready ? static_cast<float>(clip->GetFrameCount()) : 0.0f,
                    vm ? vm->normalizedTime : 0.0f,
                    ready ? 1.0f : 0.0f
                );
                std::memcpy(slot + 64, &vatParams, sizeof(glm::vec4));
            }
            wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());
        }

        // Depth-only render pass; always runs (even with zero casters) so the
        // map is cleared to 1.0 = "no occluders" and ForwardOpaquePass can
        // sample it unconditionally.
        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        WGPURenderPassDepthStencilAttachment depthAttach{};
        depthAttach.view = _shadowView;
        depthAttach.depthLoadOp = WGPULoadOp_Clear;
        depthAttach.depthStoreOp = WGPUStoreOp_Store;
        depthAttach.depthClearValue = 1.0f;
        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 0;
        passDesc.depthStencilAttachment = &depthAttach;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &passDesc);

        if (!draws.empty()) {
            GPUCommandEncoder shadowEnc;
            shadowEnc.encoder = gpuEnc->encoder;
            shadowEnc.pass = pass;
            WGPURenderPipeline boundPipeline = nullptr;
            for (size_t i = 0; i < draws.size(); ++i) {
                VATMaterial* vm = draws[i].vatMat;
                VATClip* clip = vm ? vm->clip : nullptr;
                bool ready = clip && clip->GetVertCount() > 0 && clip->GetFrameCount() > 0;
                WGPUBindGroup vatBG = ready ? _getOrCreateVATBG(clip->GetPositionTexture()) : nullptr;
                if (!vatBG) ready = false;// texture not registered yet — draw rest pose

                WGPURenderPipeline want = ready ? _vatPipeline : _pipeline;
                if (want != boundPipeline) {
                    boundPipeline = want;
                    wgpuRenderPassEncoderSetPipeline(pass, want);
                }

                uint32_t dynamicOffset = static_cast<uint32_t>(i * SHADOW_DRAW_SLOT_STRIDE);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, ready ? _vatUniformBG : _uniformBG, 1, &dynamicOffset);
                if (ready) wgpuRenderPassEncoderSetBindGroup(pass, 1, vatBG, 0, nullptr);
                draws[i].buf->Draw(&shadowEnc, PrimitiveTopology::Triangles);
            }
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return;
    }
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
    const glm::mat4 lightPV = mainLight->GetProjectionMatrix(0) * mainLight->GetViewMatrix();
    auto depthShader = ctx->GetShader("depth");
    depthShader->Activate();
    depthShader->SetUniform(std::string("ProjectionView"), lightPV);

    for (const auto& batch : batches) {
        Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
        const auto& instances = batch.instances;// NOTES: no need to check for empty instances here, as we only add
                                                // batches with instances (and OpenGL will handle 0 instances whatever)

        if (!mesh || !mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

        Material* material = ResolveMaterialOrFallback(batch.material);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (material->renderState.cull != CullMode::None) {
            glCullFace(material->renderState.cull == CullMode::Front ? GL_FRONT : GL_BACK);
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        if (mesh->type == MeshType::PRIM) {
            // VAT casters displace in a dedicated depth shader so their shadow
            // tracks the animation; every other mesh keeps the plain "depth"
            // path untouched. The per-program ProjectionView persists across
            // Activate(), so non-VAT batches need no re-set.
            VATMaterial* vatMat = dynamic_cast<VATMaterial*>(material);
            ShaderProgram* active = depthShader;
            if (vatMat) {
                active = ctx->GetShader("vat_depth");
                active->Activate();
                active->SetUniform(std::string("ProjectionView"), lightPV);
                BindVATDepthUniforms(active, vatMat);
            } else {
                depthShader->Activate();
            }

            glBindVertexArray(mesh->vao);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
            // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
            for (const auto& inst : instances) {
                active->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                    GetGLPrimitiveType(material->renderState.topology), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
                );
            }
#else
            // Upload ALL instances for this batch once
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);
            glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(InstanceData), instances.data(), GL_DYNAMIC_DRAW);

            // Instanced Draw
            glDrawElementsInstanced(
                GetGLPrimitiveType(material->renderState.topology),
                mesh->triCount * 3,
                GL_UNSIGNED_SHORT,
                nullptr,
                instances.size()
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
            const glm::mat4 facePV = l->GetProjectionMatrix(0) * l->GetViewMatrix(face);
            // Re-activate before setting per-face uniforms: a VAT batch in the
            // previous face may have left vat_depth_cubemap bound, and SetUniform
            // applies to the active program.
            depthCubemapShader->Activate();
            depthCubemapShader->SetUniform(std::string("LightPosition"), l->GetPosition());
            depthCubemapShader->SetUniform(std::string("ProjectionView"), facePV);

            for (const auto& batch : batches) {
                Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
                const auto& instances = batch.instances;

                if (!mesh || !mesh->initialized) throw std::runtime_error(fmt::format("Mesh uninitialized!"));

                Material* material = ResolveMaterialOrFallback(batch.material);

                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);

                if (material->renderState.cull != CullMode::None) {
                    glCullFace(material->renderState.cull == CullMode::Front ? GL_FRONT : GL_BACK);
                    glEnable(GL_CULL_FACE);
                } else {
                    glDisable(GL_CULL_FACE);
                }

                if (mesh->type == MeshType::PRIM) {
                    // VAT casters use the displacing cubemap depth shader; other
                    // meshes keep the plain "depth_cubemap" path. Per-program
                    // uniforms don't carry across programs, so the VAT shader
                    // gets its own LightPosition/ProjectionView set here.
                    VATMaterial* vatMat = dynamic_cast<VATMaterial*>(material);
                    ShaderProgram* active = depthCubemapShader;
                    if (vatMat) {
                        active = ctx->GetShader("vat_depth_cubemap");
                        active->Activate();
                        active->SetUniform(std::string("LightPosition"), l->GetPosition());
                        active->SetUniform(std::string("ProjectionView"), facePV);
                        BindVATDepthUniforms(active, vatMat);
                    } else {
                        depthCubemapShader->Activate();
                    }

                    glBindVertexArray(mesh->vao);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
                    // WebGL 2.0 Fallback: Non-instanced draw calls using World uniform
                    for (const auto& inst : instances) {
                        active->SetUniform(std::string("World"), inst.modelMatrix);
                        glDrawElements(
                            GetGLPrimitiveType(material->renderState.topology), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
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
                        GetGLPrimitiveType(material->renderState.topology),
                        mesh->triCount * 3,
                        GL_UNSIGNED_SHORT,
                        nullptr,
                        instances.size()
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
    for (auto& [id, entry] : _texBGCache)
        wgpuBindGroupRelease(entry.bg);
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL) wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_terrainPipeline) wgpuRenderPipelineRelease(_terrainPipeline);
    for (auto& [id, entry] : _vatBGCache)
        wgpuBindGroupRelease(entry.bg);
    if (_vatPipeline) wgpuRenderPipelineRelease(_vatPipeline);
    if (_vatBGL) wgpuBindGroupLayoutRelease(_vatBGL);
    if (_reflPipeline) wgpuRenderPipelineRelease(_reflPipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf) wgpuBufferRelease(_drawUniformBuf);
    if (_whiteTex) wgpuTextureRelease(_whiteTex);
    if (_sampler) wgpuSamplerRelease(_sampler);
    if (_shadowBG) wgpuBindGroupRelease(_shadowBG);
    if (_shadowBGL) wgpuBindGroupLayoutRelease(_shadowBGL);
    if (_shadowSampler) wgpuSamplerRelease(_shadowSampler);
}

void ForwardOpaquePass::_initGPU(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount
) {
    _gpuDevice = device;
    _gpuQueue = queue;

    {
        WGPUBufferDescriptor d{};
        d.size = FWD_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Pass-owned 1x1 white fallback texture — AssetManager::GetDefaultTextures()
    // is GL-only, so meshes with no base map sample this instead.
    {
        WGPUTextureDescriptor d{};
        d.size = { 1, 1, 1 };
        d.format = WGPUTextureFormat_RGBA8Unorm;
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension = WGPUTextureDimension_2D;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        _whiteTex = wgpuDeviceCreateTexture(device, &d);
        const uint8_t white[4] = { 255, 255, 255, 255 };
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _whiteTex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, white, 4, &layout, &extent);
    }
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        d.addressModeU = WGPUAddressMode_Repeat;
        d.addressModeV = WGPUAddressMode_Repeat;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        d.compare = WGPUCompareFunction_Less;// hardware PCF compare
        _shadowSampler = wgpuDeviceCreateSampler(device, &d);
    }

    // Standard Vertex (vertex.hpp): pos(vec3)@0, uv(vec2)@12, normal(vec3)@20,
    // tangent(vec3)@32, bitangent(vec3)@44 — stride 56. Tangent/bitangent are
    // unused by this simplified (no normal-mapping) shader but stay in the
    // buffer layout since the underlying Buffer is shared with the GL path.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(FORWARD_OPAQUE_WGSL)
                 .bgl(
                     { gpuUniform(0, wgsl_stage::both, FWD_FRAME_UNIFORM_SIZE),
                       gpuDynUniform(1, wgsl_stage::both, FWD_DRAW_UNIFORM_SIZE) }
                 )
                 // Group 1 must be vertex-visible too: the terrain pipeline borrows
                 // this BGL and its vertex shader samples the heightmap for
                 // displacement. Fragment-only visibility fails terrain pipeline
                 // creation, and one invalid pipeline poisons the whole frame's
                 // command buffer — nothing presents, black screen.
                 .bgl({ gpuTexture(0, wgsl_stage::both), gpuSampler(1, wgsl_stage::both) })
                 .bgl({ gpuDepthTexture(0), gpuCompareSampler(1) })
                 .vertex(
                     56,
                     { { WGPUVertexFormat_Float32x3, 0, 0 },
                       { WGPUVertexFormat_Float32x2, 12, 1 },
                       { WGPUVertexFormat_Float32x3, 20, 2 } }
                 )
                 .colorFormat(colorFormat)
                 .depth(true, WGPUCompareFunction_Less)
                 .cull(WGPUCullMode_Back)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL = p.bgl(1);
    _shadowBGL = p.bgl(2);

    // Terrain: the non-tessellated GLES/WebGL2 fallback (terrain_simple.vert +
    // terrain.frag) ported to WGSL. Borrows the main pipeline's group 0/1
    // layouts so the same _uniformBG (dynamic slot) and texture-BG cache
    // serve both; group 1 binds the heightmap instead of a base map. Culling
    // is off: a displaced heightfield seen from below is a hole either way,
    // and this keeps the terrain visible regardless of patch winding.
    auto tp = GpuPipelineBuilder(device)
                  .wgsl(TERRAIN_WGSL)
                  .bgl(_uniformBGL)
                  .bgl(_texBGL)
                  .vertex(56, { { WGPUVertexFormat_Float32x3, 0, 0 }, { WGPUVertexFormat_Float32x2, 12, 1 } })
                  .colorFormat(colorFormat)
                  .depth(true, WGPUCompareFunction_Less)
                  .cull(WGPUCullMode_None)
                  .multisample(sampleCount)
                  .build();
    _terrainPipeline = tp.pipeline;

    // VAT: forward shading with vertex displacement from the animation textures.
    // Reuses group 0 (frame + dynamic draw slot), group 1 (base map), group 2
    // (shadow map); group 3 adds the two rgba32float VAT textures, sampled with
    // textureLoad (unfilterable, vertex-visible). Culling off — a soft-body clip
    // can invert winding as it deforms, and dropping back-faces would punch
    // holes; the depth test still resolves overlap correctly.
    auto vp = GpuPipelineBuilder(device)
                  .wgsl(VAT_WGSL)
                  .bgl(_uniformBGL)
                  .bgl(_texBGL)
                  .bgl(_shadowBGL)
                  .bgl({ gpuUnfilterableTexture(0, wgsl_stage::vert), gpuUnfilterableTexture(1, wgsl_stage::vert) })
                  .vertex(
                      56,
                      { { WGPUVertexFormat_Float32x3, 0, 0 },
                        { WGPUVertexFormat_Float32x2, 12, 1 },
                        { WGPUVertexFormat_Float32x3, 20, 2 } }
                  )
                  .colorFormat(colorFormat)
                  .depth(true, WGPUCompareFunction_Less)
                  .cull(WGPUCullMode_None)
                  .multisample(sampleCount)
                  .build();
    _vatPipeline = vp.pipeline;
    _vatBGL = vp.bgl(3);

    // Reflection variant of the PRIM pipeline: identical except cull=None, so
    // mirrored (winding-reversed) geometry keeps all its surfaces. Borrows the
    // main pipeline's bind-group layouts.
    auto rp = GpuPipelineBuilder(device)
                  .wgsl(FORWARD_OPAQUE_WGSL)
                  .bgl(_uniformBGL)
                  .bgl(_texBGL)
                  .bgl(_shadowBGL)
                  .vertex(
                      56,
                      { { WGPUVertexFormat_Float32x3, 0, 0 },
                        { WGPUVertexFormat_Float32x2, 12, 1 },
                        { WGPUVertexFormat_Float32x3, 20, 2 } }
                  )
                  .colorFormat(colorFormat)
                  .depth(true, WGPUCompareFunction_Less)
                  .cull(WGPUCullMode_None)
                  .multisample(sampleCount)
                  .build();
    _reflPipeline = rp.pipeline;
}

WGPUBindGroup ForwardOpaquePass::_getOrCreateVATBG(uint32_t posTexID, uint32_t normTexID) {
    WGPUTexture posTex = GfxFactory::GetWGPUTexture(posTexID);
    WGPUTexture normTex = GfxFactory::GetWGPUTexture(normTexID);
    if (!posTex || !normTex) return nullptr;

    auto it = _vatBGCache.find(posTexID);
    if (it != _vatBGCache.end()) {
        if (it->second.posTex == posTex && it->second.normTex == normTex) return it->second.bg;
        wgpuBindGroupRelease(it->second.bg);
        _vatBGCache.erase(it);
    }

    WGPUBindGroup bg = GpuBindGroupBuilder(_gpuDevice, _vatBGL).texture(0, posTex).texture(1, normTex).build();
    _vatBGCache[posTexID] = { bg, posTex, normTex };
    return bg;
}

void ForwardOpaquePass::_ensureDrawCapacity(uint32_t drawCount) {
    if (drawCount <= _drawSlotCapacity && _drawUniformBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(drawCount, std::max<uint32_t>(_drawSlotCapacity * 2, 16));

    if (_drawUniformBuf) {
        wgpuBufferRelease(_drawUniformBuf);
        _drawUniformBuf = nullptr;
    }
    if (_uniformBG) {
        wgpuBindGroupRelease(_uniformBG);
        _uniformBG = nullptr;
    }

    WGPUBufferDescriptor d{};
    d.size = static_cast<uint64_t>(newCapacity) * FWD_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
                     .buffer(0, _frameUniformBuf, FWD_FRAME_UNIFORM_SIZE)
                     .buffer(1, _drawUniformBuf, FWD_DRAW_UNIFORM_SIZE)
                     .build();
}

WGPUBindGroup ForwardOpaquePass::_getOrCreateTexBG(uint32_t texID) {
    // Re-resolve every call: UpdateTexture2D recreates the WGPUTexture on
    // resize while keeping the same synthetic ID (see GPUCanvasPass).
    WGPUTexture rawTex = (texID == 0) ? _whiteTex : GfxFactory::GetWGPUTexture(texID);
    if (!rawTex) rawTex = _whiteTex;

    auto it = _texBGCache.find(texID);
    if (it != _texBGCache.end()) {
        if (it->second.tex == rawTex) return it->second.bg;
        wgpuBindGroupRelease(it->second.bg);
        _texBGCache.erase(it);
    }

    WGPUBindGroup bg = GpuBindGroupBuilder(_gpuDevice, _texBGL).texture(0, rawTex).sampler(1, _sampler).build();
    _texBGCache[texID] = { bg, rawTex };
    return bg;
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void ForwardOpaquePass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        const auto& gpuQueueCmds = renderer.GetOpaqueQueue();
        if (gpuQueueCmds.empty()) return;
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();

        ResolvedView rv = ResolveView(renderer, camera);
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, (uint32_t)rv.target->GetNumSamples());
        }

        struct DrawItem {
            Buffer* buf;
            glm::mat4 model;
            Material* mat;
            MeshType type;
        };
        std::vector<DrawItem> draws;
        draws.reserve(gpuQueueCmds.size());
        for (const auto& sortable : gpuQueueCmds) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
            // TERRAIN draws with the heightmap-displacement pipeline; VOXEL is
            // handled by VoxelChunkPass. GRASS is skipped pending a WGSL grass
            // pipeline — its blade + instance data already sit in an RHI
            // Grass-format RenderMesh (GPUBuffer slots 0/1), and
            // GpuPipelineBuilder::instance() can express the layout, so the
            // remaining work is the WGSL port of grass.vert/frag + a pipeline
            // in _initGPU wired here.
            if (!mesh) continue;
            if (mesh->type != MeshType::PRIM && mesh->type != MeshType::TERRAIN) continue;
            if (!mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            Material* effMat = ResolveMaterialOrFallback(EffectiveMaterial(cmd));
            // No HW-instanced WebGPU path yet: expand an instance span into one
            // draw per instance (each gets its own dynamic-offset uniform slot).
            if (cmd.instances && cmd.instanceCount > 0) {
                for (uint32_t k = 0; k < cmd.instanceCount; ++k)
                    draws.push_back({ buf, cmd.instances[k].modelMatrix, effMat, mesh->type });
            } else {
                draws.push_back({ buf, cmd.transform, effMat, mesh->type });
            }
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
        glm::vec3 ambient = light ? light->ambient : glm::vec3(0.1f);
        glm::mat4 viewProj = rv.proj * rv.view;

        struct {
            glm::mat4 viewProj;
            glm::mat4 lightVP;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambient;
            glm::vec4 clipPlane;
        } frameUniforms{
            viewProj,
            renderer.wgpuShadowLightVP,// published by ShadowPass earlier this frame
            glm::vec4(rv.eye, 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(ambient, 0.0f),
            rv.clipPlane,
        };
        wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &frameUniforms, sizeof(frameUniforms));

        // Shadow map bind group — rebuilt only when ShadowPass publishes a
        // different view (first frame or shadow-map recreation).
        if (!renderer.wgpuShadowMapView) return;// ShadowPass hasn't run yet
        if (renderer.wgpuShadowMapView != _shadowBGSource) {
            if (_shadowBG) wgpuBindGroupRelease(_shadowBG);
            _shadowBG = GpuBindGroupBuilder(_gpuDevice, _shadowBGL)
                            .textureView(0, renderer.wgpuShadowMapView)
                            .sampler(1, _shadowSampler)
                            .build();
            _shadowBGSource = renderer.wgpuShadowMapView;
        }

        // Pack every draw's per-instance data into its own dynamic-offset slot
        // and upload in a single call (see VOXEL_WGSL's comment).
        std::vector<uint8_t> drawData(draws.size() * FWD_DRAW_SLOT_STRIDE, 0);
        for (size_t i = 0; i < draws.size(); ++i) {
            Material* mat = draws[i].mat;
            uint8_t* slot = drawData.data() + i * FWD_DRAW_SLOT_STRIDE;
            std::memcpy(slot, &draws[i].model, sizeof(glm::mat4));

            if (draws[i].type == MeshType::TERRAIN) {
                // TERRAIN_WGSL's DrawUniforms: model + (height_scale,
                // world_size, palette_index, unused) at offset 64.
                auto* tm = dynamic_cast<TerrainMaterial*>(mat);
                glm::vec4 params(
                    tm ? tm->heightScale : 32.0f,
                    tm ? tm->worldSize : 1024.0f,
                    tm ? static_cast<float>(tm->paletteIndex) : 0.0f,
                    0.0f
                );
                std::memcpy(slot + 64, &params, sizeof(glm::vec4));
                continue;
            }

            glm::vec3 diffuse = mat ? mat->diffuse : glm::vec3(0.55f);
            glm::vec3 specular = mat ? mat->specular : glm::vec3(0.7f);
            glm::vec3 matAmbient = mat ? mat->ambient : glm::vec3(0.0f);

            glm::vec4 d4(diffuse, 0.0f), s4(specular, 0.0f), a4(matAmbient, 0.0f);
            std::memcpy(slot + 64, &d4, sizeof(glm::vec4));
            std::memcpy(slot + 80, &s4, sizeof(glm::vec4));
            std::memcpy(slot + 96, &a4, sizeof(glm::vec4));

            // Offset 112 is shininess for the standard pipeline (ignored by the
            // fs) and vatParams for VAT_WGSL. Writing vatParams for VAT materials
            // makes the VAT pipeline animate; the standard pipeline ignores it.
            if (auto* vatMat = dynamic_cast<VATMaterial*>(mat)) {
                VATClip* clip = vatMat->clip;
                bool ready = clip && clip->GetVertCount() > 0 && clip->GetFrameCount() > 0;
                glm::vec4 vatParams(
                    ready ? static_cast<float>(clip->GetVertCount()) : 0.0f,
                    ready ? static_cast<float>(clip->GetFrameCount()) : 0.0f,
                    vatMat->normalizedTime,
                    ready ? 1.0f : 0.0f
                );
                std::memcpy(slot + 112, &vatParams, sizeof(glm::vec4));
            } else {
                glm::vec4 sh4(mat ? mat->shininess : 0.25f, 0.0f, 0.0f, 0.0f);
                std::memcpy(slot + 112, &sh4, sizeof(glm::vec4));
            }
        }
        wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        // Materials without a base map fall back to the default diffuse
        // texture (checkerboard), matching what the GL path binds via
        // AssetManager::GetDefaultTextures(); plain white is the last resort.
        const auto& defaults = AssetManager::Get().GetDefaultTextures();
        uint32_t defaultDiffuse = defaults.empty() ? 0 : static_cast<uint32_t>(defaults[0]);

        // Mirrored (reflection) view reverses winding, so non-VAT PRIM draws use
        // the cull=None variant; terrain and VAT already cull None. VAT is
        // selected per draw (it needs group 3), so the pipeline is chosen inside
        // the loop rather than once up front.
        WGPURenderPipeline primPipeline = rv.flipCull ? _reflPipeline : _pipeline;

        rv.target->Begin(enc);
        WGPURenderPipeline boundPipeline = nullptr;
        for (size_t i = 0; i < draws.size(); ++i) {
            Material* mat = draws[i].mat;

            // Resolve the VAT clip (PRIM-only). If the clip isn't ready we fall
            // back to the standard pipeline so group 3 isn't required.
            VATMaterial* vatMat = (draws[i].type == MeshType::PRIM) ? dynamic_cast<VATMaterial*>(mat) : nullptr;
            VATClip* vatClip = vatMat ? vatMat->clip : nullptr;
            bool vatReady = vatClip && vatClip->GetVertCount() > 0 && vatClip->GetFrameCount() > 0;
            WGPUBindGroup vatBG =
                vatReady ? _getOrCreateVATBG(vatClip->GetPositionTexture(), vatClip->GetNormalTexture()) : nullptr;
            if (!vatBG) vatReady = false;// texture not yet registered — draw static

            WGPURenderPipeline want = draws[i].type == MeshType::TERRAIN ? _terrainPipeline
                                      : vatReady                         ? _vatPipeline
                                                                         : primPipeline;
            if (want != boundPipeline) {
                boundPipeline = want;
                wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, want);
            }

            uint32_t texID;
            if (draws[i].type == MeshType::TERRAIN) {
                // Group 1 carries the heightmap; an invalid handle falls back
                // to the white sentinel (flat terrain at full height).
                auto* tm = dynamic_cast<TerrainMaterial*>(mat);
                texID = (tm && tm->heightMap.IsValid()) ? tm->heightMap.id : 0;
            } else {
                texID = (mat && mat->baseMap.IsValid()) ? mat->baseMap.id : defaultDiffuse;
            }
            WGPUBindGroup texBG = _getOrCreateTexBG(texID);

            uint32_t dynamicOffset = static_cast<uint32_t>(i * FWD_DRAW_SLOT_STRIDE);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 1, &dynamicOffset);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 1, texBG, 0, nullptr);
            // Group 2 (shadow map) is outside the terrain pipeline's layout;
            // leaving it bound is legal and the main pipeline needs it.
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 2, _shadowBG, 0, nullptr);
            // Group 3 (VAT textures) is required by — and only by — _vatPipeline.
            if (vatReady) wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 3, vatBG, 0, nullptr);
            draws[i].buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        rv.target->End();
        return;
    }
#endif
    ZoneScopedN("ForwardOpaquePass");
    AE_GL_PROBE(renderer, "Opaque pass: entry");
    ResolvedView rv = ResolveView(renderer, ctx->GetMainCamera());
    glViewport(0, 0, rv.target->GetWidth(), rv.target->GetHeight());

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(rv.target)->GetNativeFBOID());
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
    glm::vec3 eyePos = rv.eye;
    glm::mat4 projectionView = rv.proj * rv.view;

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    AE_GL_PROBE(renderer, "Opaque pass: after clear");

    auto terrainShader = ctx->GetShader("terrain");
    auto grassShader = ctx->GetShader("grass");
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

        Material* material = ResolveMaterialOrFallback(batch.material);

        AE_GL_PROBE(renderer, fmt::format("Opaque pass: batch entry (type={})", static_cast<int>(mesh->type)));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
        if (renderer.wireframeEnabled || material->renderState.polygon == PolygonMode::Line)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

        // Mirrored (reflection) view reverses winding — disable culling so no
        // surface is wrongly removed (terrain is single-sided; a flipped
        // back-face cull would erase it from the reflection entirely).
        if (rv.flipCull || material->renderState.cull == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glCullFace(material->renderState.cull == CullMode::Front ? GL_FRONT : GL_BACK);
            glEnable(GL_CULL_FACE);
        }

        switch (mesh->type) {

        case MeshType::TERRAIN: {
            terrainShader->Activate();
            terrainShader->SetUniform(std::string("u_clipPlane"), rv.clipPlane);
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
            terrainShader->SetUniform(std::string("height_scale"), tm ? tm->heightScale : 32.0f);
            terrainShader->SetUniform(std::string("world_size"), tm ? tm->worldSize : 1024.0f);
            terrainShader->SetUniform(std::string("palette_index"), tm ? tm->paletteIndex : 0);
            terrainShader->SetUniform(std::string("fog_color"), tm ? tm->fogColor : glm::vec3(0.0f));
            terrainShader->SetUniform(std::string("fog_density"), tm ? tm->fogDensity : 0.0f);
            glActiveTexture(GL_TEXTURE7);
            TextureHandle heightMap = material->heightMap;
            if (heightMap.IsValid() && static_cast<uint32_t>(heightMap) != 0) {
                glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(heightMap));
            } else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            terrainShader->SetUniform(std::string("height_map_unit"), 7);

            // Surface maps (WorldCreator/Gaea workflow). Units follow the
            // PRIM-pass convention (2=base, 3=normal, 4=ao) plus 5=splat and
            // 8..15 for the detail layers. Every declared sampler gets a
            // valid 2D texture bound (default textures as fallback) so WebGL
            // never sees a sampler pointing at an incompatible unit.
            {
                Material* mat = material;// resolved once at the top of this batch loop
                const auto& defaults = assetManager.GetDefaultTextures();
                auto bindTex2D = [&](int unit, TextureHandle tex, int defaultIdx) {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    if (tex.IsValid() && static_cast<uint32_t>(tex) != 0)
                        glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(tex));
                    else if (defaults.size() > static_cast<size_t>(defaultIdx))
                        glBindTexture(GL_TEXTURE_2D, defaults[defaultIdx]);
                    else
                        glBindTexture(GL_TEXTURE_2D, 0);
                };

                bindTex2D(2, mat->baseMap, 0);
                terrainShader->SetUniform(std::string("base_map_unit"), 2);
                terrainShader->SetUniform(std::string("has_base_map"), mat->baseMap.IsValid() ? 1 : 0);

                bindTex2D(3, mat->normalMap, 1);
                terrainShader->SetUniform(std::string("normal_map_unit"), 3);
                terrainShader->SetUniform(std::string("has_normal_map"), mat->normalMap.IsValid() ? 1 : 0);

                bindTex2D(4, mat->aoMap, 2);
                terrainShader->SetUniform(std::string("ao_map_unit"), 4);
                terrainShader->SetUniform(std::string("has_ao_map"), mat->aoMap.IsValid() ? 1 : 0);

                TextureHandle splat = tm ? tm->splatMap : TextureHandle{};
                bindTex2D(5, splat, 0);
                terrainShader->SetUniform(std::string("splat_map_unit"), 5);
                terrainShader->SetUniform(std::string("has_splat_map"), splat.IsValid() ? 1 : 0);

                terrainShader->SetUniform(std::string("layer_count"), tm ? tm->layerCount : 0);
                for (int i = 0; i < TerrainMaterial::MAX_LAYERS; ++i) {
                    const TerrainLayer* layer = (tm && i < tm->layerCount) ? &tm->layers[i] : nullptr;
                    bindTex2D(8 + i, layer ? layer->albedoMap : TextureHandle{}, 0);
                    bindTex2D(12 + i, layer ? layer->normalMap : TextureHandle{}, 1);
                    terrainShader->SetUniform(fmt::format("layer{}_albedo_unit", i), 8 + i);
                    terrainShader->SetUniform(fmt::format("layer{}_normal_unit", i), 12 + i);
                    terrainShader->SetUniform(fmt::format("layer_tiling[{}]", i), layer ? layer->tiling : 1.0f);
                    terrainShader->SetUniform(
                        fmt::format("layer_has_normal[{}]", i), (layer && layer->normalMap.IsValid()) ? 1.0f : 0.0f
                    );
                }
            }

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

        case MeshType::GRASS: {
            // Streamed grass-blade cells (TerrainStreamer's grass ring).
            // Per-blade variation is baked into the vertex attributes; the
            // shared GrassMaterial carries the look, the wind field, and the
            // ring fade. Wind animates off renderer.frameTime.
            auto* gm = dynamic_cast<GrassMaterial*>(material);
            grassShader->Activate();
            grassShader->SetUniform(std::string("ProjectionView"), projectionView);
            grassShader->SetUniform(std::string("World"), instances[0].modelMatrix);
            grassShader->SetUniform(std::string("cam_pos"), eyePos);
            grassShader->SetUniform(std::string("u_time"), renderer.frameTime);
            grassShader->SetUniform(std::string("u_wind_dir"), gm ? gm->windDir : glm::vec2(1.0f, 0.0f));
            grassShader->SetUniform(std::string("u_wind_strength"), gm ? gm->windStrength : 0.0f);
            grassShader->SetUniform(std::string("u_wind_speed"), gm ? gm->windSpeed : 1.0f);
            grassShader->SetUniform(std::string("u_fade_start"), gm ? gm->fadeStart : 1e9f);
            grassShader->SetUniform(std::string("u_fade_end"), gm ? gm->fadeEnd : 1e9f);
            grassShader->SetUniform(std::string("u_root_color"), gm ? gm->rootColor : glm::vec3(0.2f));
            grassShader->SetUniform(std::string("u_tip_color"), gm ? gm->tipColor : glm::vec3(0.8f));
            grassShader->SetUniform(std::string("fog_color"), gm ? gm->fogColor : glm::vec3(0.0f));
            grassShader->SetUniform(std::string("fog_density"), gm ? gm->fogDensity : 0.0f);
            grassShader->SetUniform(std::string("main_light.direction"), mainLight->direction);
            grassShader->SetUniform(std::string("main_light.diffuse"), mainLight->diffuse);

            // The blade + instance data live in an RHI Grass-format RenderMesh;
            // Draw() emits the instanced draw (9 verts x instance count).
            if (mesh->UsesRenderMesh()) {
                if (Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle())) {
                    buf->Draw();
                }
            }
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
            // VAT meshes share the opaque pass but swap in the "vat" vertex
            // shader, which displaces vertices from the animation texture. The
            // fragment stage and every uniform below are identical to "color",
            // so only the shader handle and a few extra VAT uniforms differ.
            // The lookup is lazy so scenes without VAT never require the shader.
            VATMaterial* vatMat = dynamic_cast<VATMaterial*>(material);
            ShaderProgram* meshShader = vatMat ? ctx->GetShader("vat") : colorShader;

            meshShader->Activate();
            meshShader->SetUniform(std::string("u_clipPlane"), rv.clipPlane);
            meshShader->SetUniform(std::string("cam_pos"), eyePos);
            meshShader->SetUniform(std::string("time"), 0);
            meshShader->SetUniform(std::string("main_light.direction"), mainLight->direction);
            meshShader->SetUniform(std::string("main_light.ambient"), mainLight->ambient);
            meshShader->SetUniform(std::string("main_light.diffuse"), mainLight->diffuse);
            meshShader->SetUniform(std::string("main_light.specular"), mainLight->specular);
            meshShader->SetUniform(std::string("main_light.intensity"), mainLight->intensity);
            meshShader->SetUniform(std::string("main_light.cast_shadow"), mainLight->castShadow ? 1 : 0);
            meshShader->SetUniform(std::string("main_light.ProjectionView"), mainLight->GetProjectionViewMatrix(0));
            for (int i = 0; i < ctx->pointLights.size(); ++i) {
                LightComponent* l = ctx->pointLights[i];
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].position"), l->GetPosition()
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].ambient"), l->ambient
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].diffuse"), l->diffuse
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].specular"), l->specular
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].attenuation"), l->attenuation
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].intensity"), l->intensity
                );
                meshShader->SetUniform(
                    std::string("aux_lights[") + std::to_string(i) + std::string("].cast_shadow"), l->castShadow ? 1 : 0
                );
                for (int f = 0; f < 6; ++f) {
                    GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + f;
                    meshShader->SetUniform(
                        std::string("aux_lights[") + std::to_string(i) + std::string("].ProjectionViews[")
                            + std::to_string(f) + std::string("]"),
                        l->GetProjectionViewMatrix(0, face)
                    );
                }
            }
            meshShader->SetUniform(std::string("aux_light_count"), static_cast<int>(ctx->pointLights.size()));
            meshShader->SetUniform(std::string("shadow_map_unit"), 0);
            meshShader->SetUniform(std::string("omni_shadow_map_unit"), static_cast<int>(UNI_SHADOW_MAP_COUNT));
            meshShader->SetUniform(std::string("ProjectionView"), projectionView);
            // Surface parameters
            meshShader->SetUniform(std::string("surf_params.diffuse"), material->diffuse);
            meshShader->SetUniform(std::string("surf_params.specular"), material->specular);
            meshShader->SetUniform(std::string("surf_params.ambient"), material->ambient);
            meshShader->SetUniform(std::string("surf_params.shininess"), material->shininess);

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
            meshShader->SetUniform(std::string("base_map_unit"), 2);

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
            meshShader->SetUniform(std::string("normal_map_unit"), 3);

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
            meshShader->SetUniform(std::string("ao_map_unit"), 4);

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
            meshShader->SetUniform(std::string("roughness_map_unit"), 5);

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
            meshShader->SetUniform(std::string("metallic_map_unit"), 6);

            // Environment map for image-based lighting (Unit 7). Shares the
            // equirect map that SkyboxPass draws; the PBR shader samples its mip
            // chain as a cheap IBL prefilter. Invalid handle -> flat ambient.
            {
                const bool useEnv =
                    renderer.environmentMap.IsValid() && static_cast<uint32_t>(renderer.environmentMap) != 0;
                glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D, useEnv ? static_cast<uint32_t>(renderer.environmentMap) : 0);
                meshShader->SetUniform(std::string("u_envMap"), 7);
                meshShader->SetUniform(std::string("u_useEnv"), useEnv ? 1 : 0);
                meshShader->SetUniform(std::string("u_envMaxLod"), renderer.environmentMaxLod);
            }

            // VAT animation textures (units 8-9) + playback uniforms. vat.vert
            // treats vat_enabled == 0 as "use the static attributes", so a VAT
            // material with a null/invalid clip degrades to a static mesh.
            if (vatMat) {
                VATClip* clip = vatMat->clip;
                bool ready = clip && clip->GetVertCount() > 0 && clip->GetFrameCount() > 0;
                glActiveTexture(GL_TEXTURE8);
                glBindTexture(GL_TEXTURE_2D, ready ? clip->GetPositionTexture() : 0);
                meshShader->SetUniform(std::string("vat_position_map"), 8);
                glActiveTexture(GL_TEXTURE9);
                glBindTexture(GL_TEXTURE_2D, ready ? clip->GetNormalTexture() : 0);
                meshShader->SetUniform(std::string("vat_normal_map"), 9);

                meshShader->SetUniform(std::string("vat_enabled"), ready ? 1 : 0);
                meshShader->SetUniform(
                    std::string("vat_vert_count"), ready ? static_cast<int>(clip->GetVertCount()) : 0
                );
                meshShader->SetUniform(
                    std::string("vat_frame_count"), ready ? static_cast<int>(clip->GetFrameCount()) : 0
                );
                meshShader->SetUniform(std::string("vat_time"), vatMat->normalizedTime);
                // Runtime/float bakes store raw object space; remap stays off.
                meshShader->SetUniform(std::string("vat_remap"), 0);
                meshShader->SetUniform(std::string("vat_bounds_min"), glm::vec3(0.0f));
                meshShader->SetUniform(std::string("vat_bounds_max"), glm::vec3(1.0f));
            }

            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->ibo);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
            // GLES 3 / WebGL 2.0 fallback: Non-instanced draw calls using World uniform
            AE_GL_PROBE(renderer, "Opaque pass: PRIM-GLES before non-instanced loop");
            for (const auto& inst : instances) {
                meshShader->SetUniform(std::string("World"), inst.modelMatrix);
                glDrawElements(
                    GetGLPrimitiveType(material->renderState.topology), mesh->triCount * 3, GL_UNSIGNED_SHORT, 0
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
                    GetGLPrimitiveType(material->renderState.topology),
                    mesh->triCount * 3,
                    GL_UNSIGNED_SHORT,
                    nullptr,
                    instances.size()
                );
                AE_GL_PROBE(renderer, "Opaque pass: PRIM after instanced draw");
            }
#endif

            glBindVertexArray(0);
            break;
        }
    }

    // Debug lines belong to the main view only — aux-view replays (reflection,
    // portals) run before the main pass and must not consume-and-clear them.
    if (renderer.viewOverride) {
        renderer.CheckErrors("Opaque pass");
        return;
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
        Mesh* mesh = AssetManager::Get().GetMeshPtr(batch.mesh);
        const auto& instances = batch.instances;

        if (!mesh || !mesh->initialized) throw std::runtime_error("Mesh uninitialized!");

        Material* material = ResolveMaterialOrFallback(batch.material);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (material->renderState.cull != CullMode::None) {
            glCullFace(material->renderState.cull == CullMode::Front ? GL_FRONT : GL_BACK);
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        geometryShader->SetUniform(
            "ProjectionView", ctx->GetMainCamera()->GetProjectionMatrix() * ctx->GetMainCamera()->GetViewMatrix()
        );

        switch (mesh->type) {
        case MeshType::TERRAIN:
            // TODO: implement terrain rendering
            break;
        case MeshType::GRASS:
            // Grass draws in the forward opaque pass only.
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
                    GetGLPrimitiveType(material->renderState.topology),
                    mesh->triCount * 3,
                    GL_UNSIGNED_SHORT,
                    nullptr,
                    instances.size()
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
    lightingShader->SetUniform("pointLightCount", static_cast<int>(ctx->pointLights.size()));

    renderer.screenBuffer->Draw(enc, PrimitiveTopology::TriangleStrip);

    renderer.CheckErrors("Lighting pass");
}

void TransparentPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    ZoneScopedN("TransparentPass");
    auto& cam = *ctx->GetMainCamera();
    Atmospheric::CameraInfo camInfo = { .view = cam.GetViewMatrix(),
                                        .projection = cam.GetProjectionMatrix(),
                                        .position = cam.GetEyePosition() };
    Atmospheric::ParticleSubsystem::GetInstance().Draw(camInfo);// TODO: transparent pass
}

void MSAAResolvePass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
#endif
    ZoneScopedN("MSAAResolvePass");

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    // Resolve MSAA color + depth — both RTs now use GL_DEPTH_COMPONENT32F.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());
    glBindFramebuffer(
        GL_DRAW_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID()
    );
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    if (renderer.gl.glesResolvedDepthFBO != 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.gl.glesResolvedDepthFBO);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, renderer.gl.finalFBO);

    renderer.CheckErrors("MSAA resolve pass");
}

// WorldCanvasPass: World sprites with depth testing
void WorldCanvasPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        GPUCanvasPass* gpuPass = renderer.GetGPUCanvasPass();
        if (!gpuPass) return;

        // Same layer filter as the GL path below: 3D-space drawables only.
        std::vector<CanvasDrawable*> worldDrawables;
        for (auto* drawable : ctx->canvasDrawables) {
            if (!drawable->gameObject->isActive) continue;
            if ((int)drawable->GetLayer() < (int)CanvasLayer::LAYER_WORLD_2D) worldDrawables.push_back(drawable);
        }
        if (worldDrawables.empty()) return;
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        // Mirrored camera + reflection RT while PlanarReflectionPass drives us.
        ResolvedView rv = ResolveView(renderer, camera);
        glm::mat4 viewProj = rv.proj * rv.view;

        glm::vec3 camPos = rv.eye;
        std::sort(worldDrawables.begin(), worldDrawables.end(), [&camPos](CanvasDrawable* a, CanvasDrawable* b) {
            if (a->GetLayer() != b->GetLayer()) return a->GetLayer() < b->GetLayer();
            float distA = glm::length(a->gameObject->GetPosition() - camPos);
            float distB = glm::length(b->gameObject->GetPosition() - camPos);
            return distA > distB;// Back to front
        });

        // Drain through BatchRenderer2D's CPU path, same as CanvasPass.
        std::vector<BatchDrawCommand> allCommands;
        auto* br = renderer.GetBatchRenderer();
        br->StartBatch();
        for (auto* drawable : worldDrawables)
            drawable->Draw(br);
        allCommands = br->DrainToCommands();
        if (allCommands.empty()) return;

        // Render into the already-open target pass (depth-tested, read-only)
        // so world sprites are occluded by — but never occlude — 3D geometry.
        rv.target->Begin(enc);
        gpuPass->Render(
            enc,
            viewProj,
            allCommands,
            /*depthTest=*/true,
            /*toSwapchain=*/false,
            (uint32_t)rv.target->GetNumSamples()
        );
        rv.target->End();
        return;
    }
#endif
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

    // Get camera for projection; PlanarReflectionPass redirects us to the
    // reflection RT via the view override. Without an override the main path
    // targets msaaResolveRT (post-resolve, so sprites go through bloom input),
    // which ResolveView's sceneRT default would not match — so pick the target
    // explicitly here.
    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    ResolvedView rv = ResolveView(renderer, camera);
    RenderTarget* target = renderer.viewOverride ? rv.target : renderer.msaaResolveRT.get();

    glViewport(0, 0, target->GetWidth(), target->GetHeight());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(target)->GetNativeFBOID());

    glm::mat4 viewProj = rv.proj * rv.view;

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
    glm::vec3 camPos = rv.eye;
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

        // Note: buffered text is NOT drawn here — it goes through UIPass
        // (post-tonemap), matching the GL path's RenderBufferedText placement.

        // Collect 2D drawables (same layer filter as the GL path below)
        std::vector<CanvasDrawable*> drawables2D;
        for (auto* drawable : ctx->canvasDrawables) {
            if (!drawable->gameObject->isActive) continue;
            if (drawable->GetLayer() < CanvasLayer::LAYER_UI_BACK) drawables2D.push_back(drawable);
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
            allCommands.insert(
                allCommands.end(), std::make_move_iterator(drained.begin()), std::make_move_iterator(drained.end())
            );
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
        gpuPass->Render(
            enc,
            viewProj,
            allCommands,
            /*depthTest=*/false,
            /*toSwapchain=*/false,
            (uint32_t)renderer.sceneRT->GetNumSamples()
        );
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

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
PostProcessPass::~PostProcessPass() {
    if (_texBG) wgpuBindGroupRelease(_texBG);
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL) wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_sampler) wgpuSamplerRelease(_sampler);
}

void PostProcessPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat swapchainFormat) {
    _gpuDevice = device;
    _gpuQueue = queue;

    {
        WGPUBufferDescriptor d{};
        d.size = POST_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    auto p = GpuPipelineBuilder(device)
                 .wgsl(POSTPROCESS_WGSL)
                 .bgl({ gpuUniform(0, wgsl_stage::frag, POST_UNIFORM_SIZE) })
                 .bgl({ gpuTexture(0), gpuSampler(1) })
                 .colorFormat(swapchainFormat)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL = p.bgl(1);

    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, POST_UNIFORM_SIZE).build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void PostProcessPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, GfxFactory::GetSwapchainFormat());
        }
        if (!renderer.sceneRT) return;

        auto* sceneRT = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
        WGPUTexture sceneTex = sceneRT->GetNativeTexture();
        if (!sceneTex) return;

        WGPUTextureView swapchainView = GfxFactory::GetCurrentSwapchainView();
        if (!swapchainView) return;// surface not ready (device still initializing)

        // Rebuild the texture bind group only when sceneRT's texture object
        // changes (e.g. on resize) — cheap to check, avoids a per-frame alloc.
        if (sceneTex != _texBGSource) {
            if (_texBG) wgpuBindGroupRelease(_texBG);
            _texBG = GpuBindGroupBuilder(_gpuDevice, _texBGL).texture(0, sceneTex).sampler(1, _sampler).build();
            _texBGSource = sceneTex;
        }

        // Unified post-process stack (mirrors post_composite.frag): each effect
        // is an independent toggle applied in a fixed order. Bools passed as
        // 0/1 floats. Layout must match Uniforms in POSTPROCESS_WGSL.
        struct {
            float exposure, caEnabled, caStrength, time;
            float crt, vhs, grading, posterize;
            float sobel, edges, vignette, _pad;
        } u{ tonemapEnabled ? exposure : 1.0f,
             caEnabled ? 1.0f : 0.0f,
             caStrength,
             renderer.frameTime,
             crtEnabled ? 1.0f : 0.0f,
             vhsEnabled ? 1.0f : 0.0f,
             gradingEnabled ? 1.0f : 0.0f,
             posterizeEnabled ? 1.0f : 0.0f,
             sobelEnabled ? 1.0f : 0.0f,
             edgesEnabled ? 1.0f : 0.0f,
             vignetteEnabled ? 1.0f : 0.0f,
             0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        WGPURenderPassColorAttachment ca{};
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;// required for non-3D attachments
        ca.view = swapchainView;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = { renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w };
        WGPURenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &ca;
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
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);// fullscreen composite quad — never wireframe
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.msaaResolveRT->GetTextureID());

    glClearColor(renderer.clearColor.x, renderer.clearColor.y, renderer.clearColor.z, renderer.clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto shader = ctx->GetShader("post_composite");
    shader->Activate();
    shader->SetUniform(std::string("color_map_unit"), 0);
    shader->SetUniform(std::string("exposure"), tonemapEnabled ? exposure : 1.0f);
    shader->SetUniform(std::string("u_time"), renderer.frameTime);
    shader->SetUniform(std::string("u_ca_enabled"), static_cast<int>(caEnabled));
    shader->SetUniform(std::string("u_ca_strength"), caStrength);
    shader->SetUniform(std::string("u_crt_enabled"), static_cast<int>(crtEnabled));
    shader->SetUniform(std::string("u_vhs_enabled"), static_cast<int>(vhsEnabled));
    shader->SetUniform(std::string("u_grading_enabled"), static_cast<int>(gradingEnabled));
    shader->SetUniform(std::string("u_posterize_enabled"), static_cast<int>(posterizeEnabled));
    shader->SetUniform(std::string("u_sobel_enabled"), static_cast<int>(sobelEnabled));
    shader->SetUniform(std::string("u_edges_enabled"), static_cast<int>(edgesEnabled));
    shader->SetUniform(std::string("u_vignette_enabled"), static_cast<int>(vignetteEnabled));

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
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        // Text2DComponent text is already drained into the canvas queue by
        // CanvasPass::FlushTextToQueue() earlier in the render graph, so the
        // only thing left to handle here is RmlUi's queued geometry.
        auto& uiQueue = renderer.GetUIQueue();

        GPUCanvasPass* gpuPass = renderer.GetGPUCanvasPass();
        if (!gpuPass) return;

        // GPUCanvasPass::Render ignores BatchDrawCommand::transform — every
        // other producer bakes translation into vertex positions before
        // queuing. RmlUiRenderer::RenderGeometry doesn't (it reuses one
        // compiled geometry across many translated draws), so bake it here
        // via BatchRenderer2D's CPU path, matching what the GL path below
        // does by calling DrawGeometry() with cmd.transform directly.
        auto* br = renderer.GetBatchRenderer();
        br->StartBatch();
        for (const auto& cmd : uiQueue)
            br->DrawGeometry(cmd.vertices, cmd.indices, cmd.textureID, cmd.transform);
        auto allCommands = br->DrainToCommands();

        // Buffered text renders here, after tonemapping, exactly like the GL
        // path's RenderBufferedText — through sceneRT it would get tonemapped
        // (white text turns grey). Text vertices are already pre-transformed.
        ctx->FlushTextToCommands(allCommands);

        if (allCommands.empty()) return;

        WGPUTextureView swapchainView = GfxFactory::GetCurrentSwapchainView();
        if (!swapchainView) return;// surface not ready (device still initializing)

        auto logicalSize = Window::Get()->GetLogicalSize();
        glm::mat4 projection = glm::ortho(0.0f, (float)logicalSize.width, (float)logicalSize.height, 0.0f, -1.0f, 1.0f);

        // UIPass runs after PostProcessPass, which has already resolved
        // sceneRT to the swapchain — record onto the swapchain view directly
        // (Load, not Clear, so the resolved frame underneath is preserved).
        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        WGPURenderPassColorAttachment ca{};
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;// required for non-3D attachments
        ca.view = swapchainView;
        ca.loadOp = WGPULoadOp_Load;
        ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &rpDesc);
        gpuEnc->pass = pass;

        gpuPass->Render(
            enc,
            projection,
            allCommands,
            /*depthTest=*/false,
            /*toSwapchain=*/true,
            renderer.sceneRT ? (uint32_t)renderer.sceneRT->GetNumSamples() : 1u
        );

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        gpuEnc->pass = nullptr;
        wgpuTextureViewRelease(swapchainView);
        return;
    }
#endif
    ZoneScopedN("UIPass");
    auto* batchRenderer = renderer.GetBatchRenderer();
    auto& queue = renderer.GetUIQueue();

    // Setup OpenGL state for UI
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);// UI geometry — never wireframe
#endif

    // RmlUi context dimensions are in logical pixels, so projection must match.
    auto logicalSize = Window::Get()->GetLogicalSize();
    auto width = static_cast<float>(logicalSize.width);
    auto height = static_cast<float>(logicalSize.height);

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
    glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, static_cast<GLuint>(src->GetTextureID()), 0
    );
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
            rowBytes
        );
    }
    img.data = std::move(flipped);

    callback(img);
}

// ─── Async PBO readback (Phase 1 / video recording path) ────────────────────

void Renderer::schedulePixelReadback() {
    RenderTarget* src = msaaResolveRT ? msaaResolveRT.get() : sceneRT.get();
    if (!src) return;

    const auto w = static_cast<uint32_t>(src->GetWidth());
    const auto h = static_cast<uint32_t>(src->GetHeight());
    const size_t bufSize = static_cast<size_t>(w) * h * 4;

    // Lazy-init or re-init when resolution changes.
    if (m_readbackFBO == 0 || w != m_readbackPBOWidth || h != m_readbackPBOHeight) {
        destroyReadbackPBOs();

        glGenFramebuffers(1, &m_readbackFBO);
        glGenBuffers(READBACK_PBO_COUNT, m_readbackPBOs);
        for (int i = 0; i < READBACK_PBO_COUNT; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bufSize), nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        m_readbackPBOWidth = w;
        m_readbackPBOHeight = h;
        m_readbackFrameIdx = 0;
    }

    const int writeIdx = static_cast<int>(m_readbackFrameIdx % READBACK_PBO_COUNT);

    GLint prevReadFBO = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readbackFBO);
    glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, static_cast<GLuint>(src->GetTextureID()), 0
    );

    // nullptr offset = write into the bound PBO; returns immediately (async DMA).
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[writeIdx]);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFBO));

    ++m_readbackFrameIdx;
}

std::optional<GpuImageData> Renderer::collectPixelReadback() {
    // Pipeline needs READBACK_PBO_COUNT frames to prime before any data is ready.
    if (m_readbackFBO == 0 || m_readbackFrameIdx < static_cast<uint32_t>(READBACK_PBO_COUNT)) return std::nullopt;

    const uint32_t w = m_readbackPBOWidth;
    const uint32_t h = m_readbackPBOHeight;
    const size_t bufSize = static_cast<size_t>(w) * h * 4;

    // After schedulePixelReadback() incremented m_readbackFrameIdx, the oldest
    // in-flight PBO is at index m_readbackFrameIdx % READBACK_PBO_COUNT.
    // The GPU finished its DMA into this slot (READBACK_PBO_COUNT-1) frames ago.
    const int readIdx = static_cast<int>(m_readbackFrameIdx % READBACK_PBO_COUNT);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_readbackPBOs[readIdx]);
    const auto* gpuPtr = static_cast<const uint8_t*>(
        glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bufSize), GL_MAP_READ_BIT)
    );

    std::optional<GpuImageData> result;
    if (gpuPtr) {
        GpuImageData img;
        img.width = w;
        img.height = h;
        img.channelCount = 4;
        img.bottomUp = true;// raw GL data; caller uses negative sws_scale stride
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
        for (auto& pbo : m_readbackPBOs)
            pbo = 0;
    }
    m_readbackFrameIdx = 0;
    m_readbackPBOWidth = 0;
    m_readbackPBOHeight = 0;
}

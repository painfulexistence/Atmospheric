#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "frustum.hpp"
#include "gfx_factory.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "graphics_subsystem.hpp"
#include "light_component.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "sun_component.hpp"
#include "voxel_chunk_component.hpp"
#include "voxel_world.hpp"
#include "window.hpp"

#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "command_encoder.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "voxel_chunk_pass_wgsl.hpp"
#endif

// ---- Helper: build/draw the full-screen quad ---------------------------------
static void DrawScreenQuadVAO(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ---- Helper: resolve the active view for scene passes -------------------------
// PlanarReflectionPass installs renderer.viewOverride while re-rendering the
// world from a mirrored camera into its own RT; the scene passes resolve their
// camera matrices and destination target through this instead of reading the
// main camera / sceneRT directly. Declared in renderer.hpp so ForwardOpaquePass
// / WorldCanvasPass (renderer.cpp) resolve the view identically.
ResolvedView ResolveView(const Renderer& renderer, CameraComponent* camera) {
    if (const RenderViewOverride* ov = renderer.viewOverride) {
        return { ov->view,      ov->proj,    ov->eyePos, ov->target ? ov->target : renderer.sceneRT.get(),
                 ov->clipPlane, ov->flipCull };
    }
    return { camera->GetViewMatrix(),  camera->GetProjectionMatrix(),
             camera->GetEyePosition(), renderer.sceneRT.get(),
             glm::vec4(0.0f),          false };
}

// Per-draw frustum cull for voxel chunks. Chunk submission (voxel_world.cpp)
// keeps every chunk visible in the main OR any aux (portal recursion / water
// reflection) frustum, so the shared opaque queue is the union across all
// views. Each pass invocation must re-cull to its own active view — otherwise a
// chunk pulled in only for a portal's recursion frustum leaks into the main
// view as a stray floating block near the horizon. The transform for a voxel
// chunk is a pure translation to its world origin, so the bounding sphere is
// reconstructed the same way VoxelChunkComponent does.
static bool ChunkInFrustum(const Frustum& f, const glm::mat4& transform) {
    const glm::vec3 center = glm::vec3(transform[3]) + glm::vec3(VoxelChunkComponent::SIZE * 0.5f);
    return f.IntersectsSphere(center, VoxelChunkComponent::BSPHERE_RADIUS);
}


// ============================================================================
//  SunPass  (billboard quad at light direction, HDR gold for bloom glow)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
SunPass::~SunPass() {
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf) wgpuBufferRelease(_vertexBuf);
}

void SunPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;
    {
        WGPUBufferDescriptor d{};
        d.size = sizeof(SUN_QUAD_VERTS);
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        _vertexBuf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, _vertexBuf, 0, SUN_QUAD_VERTS, sizeof(SUN_QUAD_VERTS));
    }
    {
        WGPUBufferDescriptor d{};
        d.size = 192;// model(64) + viewProj(64) + color(16) + fogColor(16) + fogDensity(16) + cameraPos(16)
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    auto p = GpuPipelineBuilder(device)
                 .wgsl(SUN_WGSL)
                 .bgl({ gpuUniform(0, wgsl_stage::both, 192) })
                 .vertex(12, { { WGPUVertexFormat_Float32x3, 0, 0 } })
                 .colorFormat(colorFormat)
                 .depth(true, WGPUCompareFunction_Less)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 192).build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void SunPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();
        SunComponent* sun = ctx->GetMainSun();
        if (!sun) return;

        ResolvedView rv = ResolveView(renderer, camera);
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, (uint32_t)rv.target->GetNumSamples());
        }

        glm::vec3 lightDir = light ? glm::normalize(light->direction) : glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f));
        const float CHUNK_SIZE = 32.0f;
        const float WORLD_W = 25.0f;
        const float WORLD_D = 25.0f;
        const float CENTER_X = WORLD_W * CHUNK_SIZE * 0.5f;
        const float CENTER_Z = WORLD_D * CHUNK_SIZE * 0.5f;

        glm::vec3 sunPos(CENTER_X, sun->height, CENTER_Z);
        if (lightDir.z > 0.0f) {
            float atan_xz = lightDir.x / lightDir.z;
            sunPos.z = CENTER_Z - CHUNK_SIZE * WORLD_D;
            sunPos.x = CENTER_X - CHUNK_SIZE * WORLD_D * atan_xz;
        } else if (lightDir.z < 0.0f) {
            float atan_xz = lightDir.x / lightDir.z;
            sunPos.z = CENTER_Z + CHUNK_SIZE * WORLD_D;
            sunPos.x = CENTER_X + CHUNK_SIZE * WORLD_D * atan_xz;
        } else {
            if (lightDir.x > 0.0f)
                sunPos.x = CENTER_X - CHUNK_SIZE * WORLD_W;
            else if (lightDir.x < 0.0f)
                sunPos.x = CENTER_X + CHUNK_SIZE * WORLD_W;
            else
                sunPos.y = sun->height * 2.0f;
        }

        glm::vec3 camPos = rv.eye;
        glm::vec3 toCamera = glm::normalize(camPos - sunPos);
        glm::vec3 right = glm::normalize(glm::cross(toCamera, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::cross(right, toCamera);

        glm::mat4 model(1.0f);
        model[0] = glm::vec4(right * sun->billboardRadius, 0);
        model[1] = glm::vec4(up * sun->billboardRadius, 0);
        model[2] = glm::vec4(toCamera, 0);
        model[3] = glm::vec4(sunPos, 1);

        glm::mat4 viewProj = rv.proj * rv.view;

        struct {
            glm::mat4 model;
            glm::mat4 viewProj;
            glm::vec4 color;
            glm::vec4 fogColor;
            glm::vec4 fogDensity;
            glm::vec4 cameraPos;
        } u{
            model,
            viewProj,
            glm::vec4(sun->billboardColor, 1.0f),
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),// VX: COLOR_WHITE
            glm::vec4(0.000003f, 0.0f, 0.0f, 0.0f),
            glm::vec4(camPos, 1.0f),
        };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt = static_cast<GPURenderTarget*>(rv.target);
        rt->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(gpuEnc->pass, 0, _vertexBuf, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 6, 1, 0, 0);
        rt->End();
        return;
    }
#endif
    if (renderer.gl.skyboxVAO == 0) return;// skybox cube doubles as bounding check

    ShaderProgram* shader = AssetManager::Get().GetShader("sun");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent* light = ctx->GetMainLight();
    SunComponent* sun = ctx->GetMainSun();
    if (!sun) return;

    ResolvedView rv = ResolveView(renderer, camera);
    glViewport(0, 0, rv.target->GetWidth(), rv.target->GetHeight());

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(rv.target)->GetNativeFBOID());

    // Mirror VX sun.py get_model_matrix(): push sun to world edge along light direction
    glm::vec3 lightDir = light ? glm::normalize(light->direction) : glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f));
    const float chunkSize = 32.0f;
    const float worldW = 25.0f;
    const float worldD = 25.0f;
    const float centerX = worldW * chunkSize * 0.5f;
    const float centerZ = worldD * chunkSize * 0.5f;

    glm::vec3 sunPos(centerX, sun->height, centerZ);
    if (lightDir.z > 0.0f) {
        float atanXz = lightDir.x / lightDir.z;
        sunPos.z = centerZ - chunkSize * worldD;
        sunPos.x = centerX - chunkSize * worldD * atanXz;
    } else if (lightDir.z < 0.0f) {
        float atanXz = lightDir.x / lightDir.z;
        sunPos.z = centerZ + chunkSize * worldD;
        sunPos.x = centerX + chunkSize * worldD * atanXz;
    } else {
        if (lightDir.x > 0.0f)
            sunPos.x = centerX - chunkSize * worldW;
        else if (lightDir.x < 0.0f)
            sunPos.x = centerX + chunkSize * worldW;
        else
            sunPos.y = sun->height * 2.0f;
    }

    // Billboard: orient quad to face the (possibly mirrored) camera
    glm::vec3 camPos = rv.eye;
    glm::vec3 toCamera = glm::normalize(camPos - sunPos);
    glm::vec3 right = glm::normalize(glm::cross(toCamera, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::cross(right, toCamera);

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * sun->billboardRadius, 0);
    model[1] = glm::vec4(up * sun->billboardRadius, 0);
    model[2] = glm::vec4(toCamera, 0);
    model[3] = glm::vec4(sunPos, 1);

    glm::mat4 viewProj = rv.proj * rv.view;

    shader->Activate();
    shader->SetUniform("u_model", model);
    shader->SetUniform("u_viewProj", viewProj);
    shader->SetUniform("u_color", sun->billboardColor);
    shader->SetUniform("u_fogColor", glm::vec3(1.0f, 1.0f, 1.0f));// VX: COLOR_WHITE
    shader->SetUniform("u_fogDensity", 0.000003f);// VX: 0.000003

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);// billboard quad — never wireframe
#endif

    // Simple quad: two triangles from the skybox cube's first face vertices
    // Use a minimal inline quad VAO via screenQuadVAO trick — draw a unit quad
    static GLuint gsunVao = 0, gsunVbo = 0;
    if (gsunVao == 0) {
        float q[] = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0 };
        glGenVertexArrays(1, &gsunVao);
        glGenBuffers(1, &gsunVbo);
        glBindVertexArray(gsunVao);
        glBindBuffer(GL_ARRAY_BUFFER, gsunVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }
    glBindVertexArray(gsunVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    shader->Deactivate();
}

// ============================================================================
//  SkyboxPass  (gradient sky, rendered at depth = 1)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
SkyboxPass::~SkyboxPass() {
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf) wgpuBufferRelease(_vertexBuf);
}

void SkyboxPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;
    {
        WGPUBufferDescriptor d{};
        d.size = sizeof(SKYBOX_CUBE_VERTS);
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        _vertexBuf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, _vertexBuf, 0, SKYBOX_CUBE_VERTS, sizeof(SKYBOX_CUBE_VERTS));
    }
    {
        WGPUBufferDescriptor d{};
        d.size = 96;// viewProj(64) + skyColor(16) + horizonColor(16)
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Sky is rendered after opaque geometry at z=far; LessEqual+no-write lets
    // it fill empty pixels without overwriting opaque geometry depth.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(SKYBOX_WGSL)
                 .bgl({ gpuUniform(0, wgsl_stage::both, 96) })
                 .vertex(12, { { WGPUVertexFormat_Float32x3, 0, 0 } })
                 .colorFormat(colorFormat)
                 .depth(false, WGPUCompareFunction_LessEqual)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 96).build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void SkyboxPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;

        ResolvedView rv = ResolveView(renderer, camera);
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, (uint32_t)rv.target->GetNumSamples());
        }

        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(rv.view));
        glm::mat4 viewProj = rv.proj * viewNoTranslation;

        struct {
            glm::mat4 viewProj;
            glm::vec4 skyColor;
            glm::vec4 horizonColor;
        } u{ viewProj, glm::vec4(skyColor, 1.0f), glm::vec4(horizonColor, 1.0f) };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt = static_cast<GPURenderTarget*>(rv.target);
        rt->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(gpuEnc->pass, 0, _vertexBuf, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 36, 1, 0, 0);
        rt->End();
        return;
    }
#endif
    if (renderer.gl.skyboxVAO == 0) return;

    ShaderProgram* shader = AssetManager::Get().GetShader("skybox");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;

    ResolvedView rv = ResolveView(renderer, camera);
    glViewport(0, 0, rv.target->GetWidth(), rv.target->GetHeight());

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(rv.target)->GetNativeFBOID());

    shader->Activate();

    // Strip translation from view matrix so sky doesn't move with camera
    glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(rv.view));
    shader->SetUniform("u_proj", rv.proj);
    shader->SetUniform("u_view", viewNoTranslation);
    shader->SetUniform("u_skyColor", skyColor);
    shader->SetUniform("u_horizonColor", horizonColor);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);// fullscreen gradient — never wireframe
#endif

    glBindVertexArray(renderer.gl.skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    shader->Deactivate();
}

// ============================================================================
//  VoxelChunkPass
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
VoxelChunkPass::~VoxelChunkPass() {
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf) wgpuBufferRelease(_drawUniformBuf);
}

void VoxelChunkPass::_initGPU(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount, WGPUCullMode cullMode
) {
    _gpuDevice = device;
    _gpuQueue = queue;
    {
        WGPUBufferDescriptor d{};
        d.size = VOXEL_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // VoxelVertex: x,y,z,voxel_id + face_id,pad[3] — two Uint8x4 attrs since
    // WebGPU has no Uint8x3 / plain uint8 format.
    // cullMode is Front instead of Back when this instance renders
    // PlanarReflectionPass's mirrored view (reversed triangle winding).
    auto p = GpuPipelineBuilder(device)
                 .wgsl(VOXEL_WGSL)
                 .bgl(
                     { gpuUniform(0, wgsl_stage::both, VOXEL_FRAME_UNIFORM_SIZE),
                       gpuDynUniform(1, wgsl_stage::vert, VOXEL_DRAW_UNIFORM_SIZE) }
                 )
                 .vertex(8, { { WGPUVertexFormat_Uint8x4, 0, 0 }, { WGPUVertexFormat_Uint8x4, 4, 1 } })
                 .colorFormat(colorFormat)
                 .depth(true, WGPUCompareFunction_Less)
                 .cull(cullMode)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
}

void VoxelChunkPass::_ensureDrawCapacity(uint32_t drawCount) {
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
    d.size = static_cast<uint64_t>(newCapacity) * VOXEL_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
                     .buffer(0, _frameUniformBuf, VOXEL_FRAME_UNIFORM_SIZE)
                     .buffer(1, _drawUniformBuf, VOXEL_DRAW_UNIFORM_SIZE)
                     .build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void VoxelChunkPass::RegisterWorld(VoxelWorld* w) {
    if (!w) return;
    for (auto* existing : _giWorlds)
        if (existing == w) return;
    _giWorlds.push_back(w);
}

void VoxelChunkPass::UnregisterWorld(VoxelWorld* w) {
    for (size_t i = 0; i < _giWorlds.size(); i++) {
        if (_giWorlds[i] == w) {
            _giWorlds.erase(_giWorlds.begin() + static_cast<long>(i));
            break;
        }
    }
}

void VoxelChunkPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
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
            _initGPU(
                dev,
                q,
                WGPUTextureFormat_RGBA16Float,
                (uint32_t)rv.target->GetNumSamples(),
                rv.flipCull ? WGPUCullMode_Front : WGPUCullMode_Back
            );
        }

        const Frustum viewFrustum(rv.proj * rv.view);
        struct DrawItem {
            Buffer* buf;
            glm::mat4 model;
        };
        std::vector<DrawItem> draws;
        draws.reserve(gpuQueueCmds.size());
        for (const auto& sortable : gpuQueueCmds) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
            if (!mesh || mesh->type != MeshType::VOXEL) continue;
            if (!mesh->UsesRenderMesh()) continue;
            if (!ChunkInFrustum(viewFrustum, cmd.transform)) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            draws.push_back({ buf, cmd.transform });
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
        glm::vec3 ambient = light ? light->ambient * 0.15f : glm::vec3(0.1f);
        glm::mat4 viewProj = rv.proj * rv.view;

        struct {
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambient;
            glm::vec4 fogColor;
            glm::vec4 fogDensity;
            glm::vec4 clipPlane;
        } frameUniforms{
            viewProj,
            glm::vec4(rv.eye, 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(ambient, 0.0f),
            glm::vec4(0.55f, 0.65f, 0.75f, 1.0f),
            glm::vec4(0.003f, 0.0f, 0.0f, 0.0f),
            rv.clipPlane,
        };
        wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &frameUniforms, sizeof(frameUniforms));

        // Pack every draw's model matrix into its own dynamic-offset slot and
        // upload in a single call (see the comment above VOXEL_WGSL).
        std::vector<uint8_t> drawData(draws.size() * VOXEL_DRAW_SLOT_STRIDE, 0);
        for (size_t i = 0; i < draws.size(); ++i) {
            std::memcpy(drawData.data() + i * VOXEL_DRAW_SLOT_STRIDE, &draws[i].model, sizeof(glm::mat4));
        }
        wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt = static_cast<GPURenderTarget*>(rv.target);
        rt->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        for (size_t i = 0; i < draws.size(); ++i) {
            uint32_t dynamicOffset = static_cast<uint32_t>(i * VOXEL_DRAW_SLOT_STRIDE);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 1, &dynamicOffset);
            draws[i].buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        rt->End();
        return;
    }
#endif
    const auto& queue = renderer.GetOpaqueQueue();
    if (queue.empty()) return;

    ShaderProgram* shader = AssetManager::Get().GetShader("voxel");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent* light = ctx->GetMainLight();

    ResolvedView rv = ResolveView(renderer, camera);
    glViewport(0, 0, rv.target->GetWidth(), rv.target->GetHeight());

    // Render into the same target as ForwardOpaquePass (no clear — already done)
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(rv.target)->GetNativeFBOID());

    shader->Activate();

    glm::mat4 viewProj = rv.proj * rv.view;
    shader->SetUniform("u_viewProj", viewProj);
    shader->SetUniform("u_cameraPos", rv.eye);
    shader->SetUniform("u_clipPlane", rv.clipPlane);

    glm::vec3 lightDir = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
    glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
    glm::vec3 ambient = light ? light->ambient * 0.15f : glm::vec3(0.1f);
    shader->SetUniform("u_lightDir", lightDir);
    shader->SetUniform("u_lightColor", lightColor);
    shader->SetUniform("u_ambientColor", ambient);
    // VX ties terrain/water fog color to the skybox's sky gradient color every frame.
    auto* skybox = renderer.GetPass<SkyboxPass>();
    shader->SetUniform("u_fogColor", skybox ? skybox->skyColor : glm::vec3(0.686f, 0.933f, 0.933f));
    shader->SetUniform("u_fogDensity", 0.00001f);// VX: scene.py u_fog_density
    shader->SetUniform("u_aoStrength", aoEnabled ? aoStrength : 0.0f);

    // Palette lives on the first voxel command's VoxelMaterial (one palette per
    // world per queue). Fallback keeps the historical default alive if a queue
    // slipped in without a VoxelMaterial (test fixtures, out-of-tree examples).
    int palette = 4;
    for (const auto& sortable : queue) {
        Mesh* m = AssetManager::Get().GetMeshPtr(sortable.cmd.mesh);
        if (!m || m->type != MeshType::VOXEL) continue;
        if (auto* vm = dynamic_cast<VoxelMaterial*>(AssetManager::Get().ResolveMaterial(m->GetMaterial()))) {
            palette = vm->paletteIndex;
        }
        break;
    }
    shader->SetUniform("u_paletteIndex", palette);

    // ── Global illumination (VoxelGI / VCT) ──────────────────────────────────
    // For VoxelGI, drive the registered world's radiance grid (inject on
    // dirty/camera-move) and cone-trace it in voxel.frag. SSGI is reserved
    // (mode 1, a shader no-op for now). u_giRadiance is always bound to a valid
    // 3D texture so strict drivers never see an unbound sampler.
    shader->SetUniform("u_giStrength", giStrength);
    int giModeInt = static_cast<int>(giMode);
    if (giMode == GIMode::VoxelGI && !_giWorlds.empty()) {
        VoxelWorld* w = _giWorlds[0];
        w->coneGI.slabsPerFrame = std::max(1, giInjectSlabs);
        // sunStrength 1.0: the rasterizer shades with lightColor directly (no
        // intensity multiply), so injected direct light stays consistent.
        const bool ready =
            w->coneGI.Update(*w, rv.eye, lightDir, lightColor, 1.0f, ambient, palette, w->giDirty, giVoxelDim);
        w->giDirty = false;
        if (ready) {
            w->coneGI.Bind(shader, 0);// u_giRadiance + placement on unit 0
        } else {
            giModeInt = 0;// grid not ready yet — fall back to flat ambient
        }
    }
    if (giModeInt != 2) {
        if (_giFallbackTex == 0) {
            glGenTextures(1, &_giFallbackTex);
            glBindTexture(GL_TEXTURE_3D, _giFallbackTex);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, 1, 1, 1, 0, GL_RGBA, GL_FLOAT, zero);
            glBindTexture(GL_TEXTURE_3D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, _giFallbackTex);
        shader->SetUniform("u_giRadiance", 0);
        shader->SetUniform("u_giOrigin", glm::vec3(0.0f));
        shader->SetUniform("u_giCellSize", 1.0f);
        shader->SetUniform("u_giDim", 1);
        shader->SetUniform("u_giMaxMip", 0.0f);
    }
    shader->SetUniform("u_giMode", giModeInt);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    // Mirrored views reverse triangle winding, so cull front faces instead.
    glCullFace(rv.flipCull ? GL_FRONT : GL_BACK);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    // Opt into wireframe explicitly; don't rely on GL_LINE leaking from
    // ForwardOpaquePass, since upstream passes now reset polygon mode themselves.
    glPolygonMode(GL_FRONT_AND_BACK, renderer.wireframeEnabled ? GL_LINE : GL_FILL);
#endif

    const Frustum viewFrustum(viewProj);
    for (const auto& sortable : queue) {
        const auto& cmd = sortable.cmd;
        Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);

        if (!mesh || mesh->type != MeshType::VOXEL) continue;
        if (!mesh->UsesRenderMesh()) continue;
        if (!ChunkInFrustum(viewFrustum, cmd.transform)) continue;

        shader->SetUniform("u_model", cmd.transform);

        Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
        if (buf && buf->IsInitialized() && buf->GetVertexCount() > 0) {
            buf->Draw(nullptr, PrimitiveTopology::Triangles);
        }
    }

    if (rv.flipCull) glCullFace(GL_BACK);// don't leak the mirrored cull state
    shader->Deactivate();
}

// ============================================================================
//  WaterPass
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
WaterPass::~WaterPass() {
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf) wgpuBufferRelease(_drawUniformBuf);
    if (_reflBG) wgpuBindGroupRelease(_reflBG);
    if (_reflBGL) wgpuBindGroupLayoutRelease(_reflBGL);
    if (_reflFallbackTex) wgpuTextureRelease(_reflFallbackTex);
    if (_reflSampler) wgpuSamplerRelease(_reflSampler);
}

void WaterPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;
    {
        WGPUBufferDescriptor d{};
        d.size = WATER_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Depth-tested, no depth write — water is occluded by opaque geometry but
    // never occludes itself/other transparents (mirrors GL glDepthMask(GL_FALSE)).
    // Group 1 holds the planar-reflection texture written by PlanarReflectionPass.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(WATER_WGSL)
                 .bgl(
                     { gpuUniform(0, wgsl_stage::both, WATER_FRAME_UNIFORM_SIZE),
                       gpuDynUniform(1, wgsl_stage::both, WATER_DRAW_UNIFORM_SIZE) }
                 )
                 .bgl({ gpuTexture(0), gpuSampler(1) })
                 .vertex(
                     56,
                     { { WGPUVertexFormat_Float32x3, 0, 0 },
                       { WGPUVertexFormat_Float32x2, 12, 1 },
                       { WGPUVertexFormat_Float32x3, 20, 2 } }
                 )
                 .colorFormat(colorFormat)
                 .blend()
                 .depth(false, WGPUCompareFunction_LessEqual)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _reflBGL = p.bgl(1);

    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _reflSampler = wgpuDeviceCreateSampler(device, &d);
    }
    // 1x1 black fallback bound while PlanarReflectionPass is inactive; the
    // per-draw reflection strength is zeroed then, so it's never visible —
    // it only satisfies the pipeline's group-1 layout.
    {
        WGPUTextureDescriptor d{};
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension = WGPUTextureDimension_2D;
        d.size = { 1, 1, 1 };
        d.format = WGPUTextureFormat_RGBA16Float;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        _reflFallbackTex = wgpuDeviceCreateTexture(device, &d);

        const uint16_t black[4] = { 0, 0, 0, 0 };// f16 zero bits
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _reflFallbackTex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = sizeof(black);
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, black, sizeof(black), &layout, &extent);
    }
}

void WaterPass::_ensureDrawCapacity(uint32_t drawCount) {
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
    d.size = static_cast<uint64_t>(newCapacity) * WATER_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
                     .buffer(0, _frameUniformBuf, WATER_FRAME_UNIFORM_SIZE)
                     .buffer(1, _drawUniformBuf, WATER_DRAW_UNIFORM_SIZE)
                     .build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void WaterPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        const auto& queue = renderer.GetTransparentQueue();
        if (queue.empty()) return;
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();

        // Auxiliary views (portal recursion) drive us via viewOverride: render
        // into the aux RT, reflection off (it was computed for the main camera).
        // The WGSL water is already depth-less, so no per-view depth is needed.
        ResolvedView rv = ResolveView(renderer, camera);
        bool auxView = renderer.viewOverride != nullptr;

        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, (uint32_t)rv.target->GetNumSamples());
        }

        // Water params now live on WaterMaterial (mesh/material decoupling);
        // fall back to WaterMaterial's own defaults when the mesh carries a
        // plain Material, mirroring the GL path's `wm ? ... : default` chain.
        struct DrawItem {
            Buffer* buf;
            glm::mat4 model;
            WaterMaterial wd;
        };
        std::vector<DrawItem> draws;
        draws.reserve(queue.size());
        for (const auto& sortable : queue) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
            if (!mesh || mesh->type != MeshType::PRIM) continue;
            Material* mat = AssetManager::Get().ResolveMaterial(mesh->GetMaterial());
            if (!mat || mat->renderQueue != RenderQueue::Transparent) continue;
            if (dynamic_cast<PortalMaterial*>(mat)) continue;// drawn by PortalSurfacePass
            if (!mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            auto* wm = dynamic_cast<WaterMaterial*>(mat);
            WaterMaterial wd = wm ? *wm : WaterMaterial{};
            if (!wm) {
                // Reflection opt-in lives on the base Material; don't let the
                // WaterMaterial fallback's default-on override a plain
                // Material's choice.
                wd.planarReflection = mat->planarReflection;
                wd.reflectionStrength = mat->reflectionStrength;
                wd.reflectionDistortion = mat->reflectionDistortion;
            }
            draws.push_back({ buf, cmd.transform, wd });
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
        glm::mat4 viewProj = rv.proj * rv.view;

        // Planar reflection RT, rendered earlier by PlanarReflectionPass. Off in
        // aux views (that reflection is the main camera's).
        auto* refl = renderer.GetPass<PlanarReflectionPass>();
        bool reflActive = refl && refl->IsActive() && refl->GetReflectionRT() && !auxView;
        glm::mat4 reflViewProj = reflActive ? refl->GetReflectionViewProj() : glm::mat4(1.0f);

        struct {
            glm::mat4 viewProj;
            glm::mat4 reflViewProj;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 time;
        } frameUniforms{
            viewProj,
            reflViewProj,
            glm::vec4(camera->GetEyePosition(), 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(renderer.frameTime, 0.0f, 0.0f, 0.0f),
        };
        wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &frameUniforms, sizeof(frameUniforms));

        // Bind group 1: reflection RT color texture (or the 1x1 fallback).
        // Rebuilt only when the underlying WGPUTexture changes (RT resize /
        // active-state flip), mirroring ForwardOpaquePass's CachedTexBG.
        WGPUTexture reflTex =
            reflActive ? static_cast<GPURenderTarget*>(refl->GetReflectionRT())->GetNativeTexture() : _reflFallbackTex;
        if (!_reflBG || _reflBGSource != reflTex) {
            if (_reflBG) wgpuBindGroupRelease(_reflBG);
            _reflBG = GpuBindGroupBuilder(_gpuDevice, _reflBGL).texture(0, reflTex).sampler(1, _reflSampler).build();
            _reflBGSource = reflTex;
        }

        // Fog color is no longer stored on WaterMaterial — read it live from
        // SkyboxPass::skyColor each frame, mirroring the GL path below.
        SkyboxPass* skybox = renderer.GetPass<SkyboxPass>();
        glm::vec3 skyFogColor = skybox ? skybox->skyColor : glm::vec3(0.686f, 0.933f, 0.933f);

        std::vector<uint8_t> drawData(draws.size() * WATER_DRAW_SLOT_STRIDE, 0);
        for (size_t i = 0; i < draws.size(); ++i) {
            const auto& wd = draws[i].wd;
            uint8_t* slot = drawData.data() + i * WATER_DRAW_SLOT_STRIDE;
            std::memcpy(slot, &draws[i].model, sizeof(glm::mat4));
            glm::vec4 params0(wd.waterLine, wd.waveStrength, wd.waveSpeed, wd.waterFogDensity);
            glm::vec4 fogColor(skyFogColor, 0.0f);
            glm::vec4 params1(
                wd.reflectionStrength, wd.reflectionDistortion, (reflActive && wd.planarReflection) ? 1.0f : 0.0f, 0.0f
            );
            std::memcpy(slot + 64, &params0, sizeof(glm::vec4));
            std::memcpy(slot + 80, &fogColor, sizeof(glm::vec4));
            std::memcpy(slot + 96, &params1, sizeof(glm::vec4));
        }
        wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        rv.target->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 1, _reflBG, 0, nullptr);
        for (size_t i = 0; i < draws.size(); ++i) {
            uint32_t dynamicOffset = static_cast<uint32_t>(i * WATER_DRAW_SLOT_STRIDE);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 1, &dynamicOffset);
            draws[i].buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        rv.target->End();
        return;
    }
#endif
    const auto& queue = renderer.GetTransparentQueue();
    if (queue.empty()) return;

    ShaderProgram* shader = AssetManager::Get().GetShader("water");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent* light = ctx->GetMainLight();

    // Auxiliary views (portal recursion) drive us via viewOverride: render into
    // the aux RT with reflection off (it was computed for the main camera). The
    // main view still renders into msaaResolveRT (post-resolve, through bloom).
    ResolvedView rv = ResolveView(renderer, camera);
    bool auxView = renderer.viewOverride != nullptr;
    RenderTarget* target = auxView ? rv.target : renderer.msaaResolveRT.get();

    glViewport(0, 0, target->GetWidth(), target->GetHeight());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(target)->GetNativeFBOID());

    // Beer-Lambert needs the opaque scene depth. Main view: the resolved depth.
    // Aux view: the aux RT's own depth attachment, which desktop GL permits
    // sampling read-only while the same target is bound (exactly what the main
    // view does with msaaResolveRT). This gives portal water the same depth
    // gradient as the main view instead of a flat fresnel fallback. GLES/WebGL
    // forbid that feedback loop, so they keep the fresnel fallback there.
    GLuint depthTex = renderer.GetResolvedDepthTexture();
    float useDepth = auxView ? 0.0f : 1.0f;
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    if (auxView) {
        GLuint auxDepth = static_cast<GLuint>(rv.target->GetDepthTextureID());
        if (auxDepth != 0) {
            depthTex = auxDepth;
            useDepth = 1.0f;
        }
    }
#endif

    shader->Activate();

    glm::mat4 proj = rv.proj;
    glm::mat4 view = rv.view;
    glm::mat4 viewProj = proj * view;
    shader->SetUniform("u_viewProj", viewProj);
    shader->SetUniform("u_cameraPos", rv.eye);
    shader->SetUniform("u_time", renderer.frameTime);
    shader->SetUniform("u_invProj", glm::inverse(proj));
    shader->SetUniform("u_invView", glm::inverse(view));
    shader->SetUniform("u_screenSize", glm::vec2((float)target->GetWidth(), (float)target->GetHeight()));
    shader->SetUniform("u_depthTexture", 1);
    shader->SetUniform("u_useDepth", useDepth);

    glm::vec3 lightDir = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
    glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
    shader->SetUniform("u_lightDir", lightDir);
    shader->SetUniform("u_lightColor", lightColor);

    // VX ties terrain/water fog color to the skybox's sky gradient color every frame.
    auto* skybox = renderer.GetPass<SkyboxPass>();
    glm::vec3 skyFogColor = skybox ? skybox->skyColor : glm::vec3(0.686f, 0.933f, 0.933f);

    // Planar reflection RT, rendered earlier by PlanarReflectionPass. Off in
    // aux views: that reflection was computed for the main camera, so sampling
    // it from a portal camera would show the wrong scene.
    auto* refl = renderer.GetPass<PlanarReflectionPass>();
    bool reflActive = refl && refl->IsActive() && refl->GetReflectionRT() && !auxView;
    shader->SetUniform("u_reflectionTex", 2);
    if (reflActive) {
        shader->SetUniform("u_reflViewProj", refl->GetReflectionViewProj());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(refl->GetReflectionRT()->GetTextureID()));
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTex);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    // Opt into wireframe explicitly rather than inheriting leaked polygon mode.
    glPolygonMode(GL_FRONT_AND_BACK, renderer.wireframeEnabled ? GL_LINE : GL_FILL);
#endif

    for (const auto& s : queue) {
        const auto& cmd = s.cmd;
        Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
        if (!mesh) continue;

        Material* mat = AssetManager::Get().ResolveMaterial(mesh->GetMaterial());
        if (!mat || mat->renderQueue != RenderQueue::Transparent) continue;
        if (dynamic_cast<PortalMaterial*>(mat)) continue;// drawn by PortalSurfacePass

        auto* wm = dynamic_cast<WaterMaterial*>(AssetManager::Get().ResolveMaterial(mesh->GetMaterial()));
        shader->SetUniform("u_waterLine", wm ? wm->waterLine : 32.0f);
        shader->SetUniform("u_waveStrength", wm ? wm->waveStrength : 0.1f);
        shader->SetUniform("u_waveSpeed", wm ? wm->waveSpeed : 1.0f);
        shader->SetUniform("u_fogColor", skyFogColor);
        shader->SetUniform("u_fogDensity", wm ? wm->waterFogDensity : 0.00001f);
        shader->SetUniform("u_deepColor", wm ? wm->deepColor : glm::vec3{ 0.05f, 0.1f, 0.25f });
        shader->SetUniform("u_shallowColor", wm ? wm->shallowColor : glm::vec3{ 0.686f, 0.933f, 0.933f });
        shader->SetUniform("u_beerCoef", wm ? wm->beerCoef : 0.095f);
        // Reflection opt-in lives on the base Material, so a plain Material
        // water surface can still request it.
        bool wantsRefl = reflActive && mat->planarReflection;
        shader->SetUniform("u_reflStrength", wantsRefl ? mat->reflectionStrength : 0.0f);
        shader->SetUniform("u_reflDistortion", mat->reflectionDistortion);
        shader->SetUniform("u_model", cmd.transform);

        glBindVertexArray(mesh->vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->triCount * 3), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    shader->Deactivate();
}

// ============================================================================
//  PlanarReflectionPass
// ============================================================================
PlanarReflectionPass::PlanarReflectionPass()
  : _forward(std::make_unique<ForwardOpaquePass>()), _skybox(std::make_unique<SkyboxPass>()),
    _sun(std::make_unique<SunPass>()), _voxel(std::make_unique<VoxelChunkPass>()),
    _worldCanvas(std::make_unique<WorldCanvasPass>()) {
}

PlanarReflectionPass::~PlanarReflectionPass() = default;

void PlanarReflectionPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    _active = false;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;

    // Find the first draw whose material opts in; its transform defines the
    // mirror plane (position + up axis). Water sits in the transparent queue,
    // so scan that first; a future opaque mirror is found via the opaque queue.
    glm::mat4 reflTransform(1.0f);
    auto scan = [&](const auto& queue) {
        for (const auto& s : queue) {
            Mesh* mesh = AssetManager::Get().GetMeshPtr(s.cmd.mesh);
            if (!mesh) continue;
            Material* mat = AssetManager::Get().ResolveMaterial(mesh->GetMaterial());
            if (mat && mat->planarReflection) {
                reflTransform = s.cmd.transform;
                return true;
            }
        }
        return false;
    };
    if (!scan(renderer.GetTransparentQueue()) && !scan(renderer.GetOpaqueQueue())) return;

    glm::vec3 planeN = glm::normalize(glm::vec3(reflTransform[1]));
    glm::vec3 planeP = glm::vec3(reflTransform[3]);
    float planeD = -glm::dot(planeN, planeP);

    glm::vec3 eye = camera->GetEyePosition();
    float eyeDist = glm::dot(planeN, eye) + planeD;
    if (eyeDist <= 0.0f) return;// camera behind/on the plane — nothing to mirror

    // (Re)create the reflection RT at a fraction of the window size. HDR to
    // match sceneRT's shading range.
    // Sample count: single-sampled on GL (a desktop MSAA texture can't be read
    // through a plain sampler2D, and WebGL2 MSAA RTs can't be sampled at all).
    // On WebGPU it matches sceneRT's sample count instead — an MSAA RT resolves
    // to a sampleable single-sample texture, and matching keeps the shared
    // GPUCanvasPass (WorldCanvasPass) on one pipeline sample count across the
    // reflection and main views.
    int rtSamples = (GfxFactory::GetBackend() == GfxBackend::WebGPU) ? renderer.sceneRT->GetNumSamples() : 1;
    auto [pw, ph] = Window::Get()->GetPhysicalSize();
    int rw = std::max(1, static_cast<int>(static_cast<float>(pw) * resolutionScale));
    int rh = std::max(1, static_cast<int>(static_cast<float>(ph) * resolutionScale));
    if (!_rt) {
        RenderTarget::Props p;
        p.width = rw;
        p.height = rh;
        p.withDepth = true;
        p.hdr = true;
        p.numSamples = rtSamples;
        _rt = GfxFactory::CreateRenderTarget(p);
    } else if (_rt->GetWidth() != rw || _rt->GetHeight() != rh) {
        _rt->Resize(rw, rh);
    }
    if (!_rt || !_rt->IsValid()) return;

    // Householder reflection about the plane: linear part I - 2*n*n^T,
    // translation -2*d*n. view * R renders the world as seen by the eye
    // mirrored to the other side of the plane.
    glm::mat4 R(1.0f);
    R[0][0] = 1.0f - 2.0f * planeN.x * planeN.x;
    R[0][1] = -2.0f * planeN.x * planeN.y;
    R[0][2] = -2.0f * planeN.x * planeN.z;
    R[1][0] = -2.0f * planeN.y * planeN.x;
    R[1][1] = 1.0f - 2.0f * planeN.y * planeN.y;
    R[1][2] = -2.0f * planeN.y * planeN.z;
    R[2][0] = -2.0f * planeN.z * planeN.x;
    R[2][1] = -2.0f * planeN.z * planeN.y;
    R[2][2] = 1.0f - 2.0f * planeN.z * planeN.z;
    R[3][0] = -2.0f * planeD * planeN.x;
    R[3][1] = -2.0f * planeD * planeN.y;
    R[3][2] = -2.0f * planeD * planeN.z;

    RenderViewOverride ov;
    ov.view = camera->GetViewMatrix() * R;
    ov.proj = camera->GetProjectionMatrix();
    ov.eyePos = eye - 2.0f * eyeDist * planeN;
    ov.target = _rt.get();
    // Keep only geometry on the reflective side. The slack keeps fragments up
    // to 0.05 below the plane so wave displacement doesn't open seams at the
    // shoreline.
    ov.clipPlane = glm::vec4(planeN, planeD + 0.05f);
    ov.flipCull = true;
    _reflViewProj = ov.proj * ov.view;

    // The private sub-pass instances mirror the graph instances' tunables.
    // Palette no longer needs mirroring — it lives on the shared VoxelMaterial
    // that both main and reflection VoxelChunkPass instances read via their
    // render queue's first command.
    auto* mainSky = renderer.GetPass<SkyboxPass>();
    if (mainSky) {
        _skybox->skyColor = mainSky->skyColor;
        _skybox->horizonColor = mainSky->horizonColor;
    }

    // Mirror the main render graph's world order so the reflection contains the
    // same content: opaque terrain/meshes first (ForwardOpaquePass also clears
    // the RT), then sky/sun/voxels, then world sprites. Anything that reads
    // renderer.viewOverride draws from the mirrored camera into _rt.
    renderer.viewOverride = &ov;
    _rt->Clear(glm::vec4(_skybox->skyColor, 1.0f));
    _forward->Execute(ctx, renderer, enc);
    _skybox->Execute(ctx, renderer, enc);
    _sun->Execute(ctx, renderer, enc);
    _voxel->Execute(ctx, renderer, enc);
    _worldCanvas->Execute(ctx, renderer, enc);
    renderer.viewOverride = nullptr;

    _active = true;
}

// ============================================================================
//  BloomPass  (pyramid downsample + upsample + ACES composite)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
void BloomPass::_destroyGPUTextures() {
    // Bind groups first — they reference the mip views / snapshot below.
    if (_threshBG) {
        wgpuBindGroupRelease(_threshBG);
        _threshBG = nullptr;
    }
    for (int i = 0; i < MIP_LEVELS; ++i) {
        if (_downBG[i]) {
            wgpuBindGroupRelease(_downBG[i]);
            _downBG[i] = nullptr;
        }
        if (_upBG[i]) {
            wgpuBindGroupRelease(_upBG[i]);
            _upBG[i] = nullptr;
        }
    }
    if (_compBG) {
        wgpuBindGroupRelease(_compBG);
        _compBG = nullptr;
    }
    for (int i = 0; i < MIP_LEVELS; ++i) {
        if (_gpuMips[i].view) {
            wgpuTextureViewRelease(_gpuMips[i].view);
            _gpuMips[i].view = nullptr;
        }
        if (_gpuMips[i].tex) {
            wgpuTextureRelease(_gpuMips[i].tex);
            _gpuMips[i].tex = nullptr;
        }
    }
    if (_snapshotView) {
        wgpuTextureViewRelease(_snapshotView);
        _snapshotView = nullptr;
    }
    if (_snapshotTex) {
        wgpuTextureRelease(_snapshotTex);
        _snapshotTex = nullptr;
    }
}

void BloomPass::_initGPU(WGPUDevice device, WGPUQueue queue, uint32_t sceneSampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;

    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    auto makeUniformBuf = [device](uint64_t size) {
        WGPUBufferDescriptor d{};
        d.size = size;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        return wgpuDeviceCreateBuffer(device, &d);
    };
    _threshUniformBuf = makeUniformBuf(16);
    _compUniformBuf = makeUniformBuf(16);
    // One texelSize uniform per down/up step (indices 1..MIP_LEVELS-1); content
    // is resolution-dependent and written in _resizeGPU, not per frame.
    for (int i = 1; i < MIP_LEVELS; ++i) {
        _downUniformBuf[i] = makeUniformBuf(16);
        _upUniformBuf[i] = makeUniformBuf(16);
    }

    // Thresh, downsample and upsample all share the same BGL shape:
    // 1 uniform + 1 texture + 1 sampler.
    std::vector<GpuBGLEntry> singleTexBGL = {
        gpuUniform(0, wgsl_stage::frag, 16),
        gpuTexture(1),
        gpuSampler(2),
    };
    {
        auto p = GpuPipelineBuilder(device)
                     .wgsl(BLOOM_THRESH_WGSL)
                     .bgl(singleTexBGL)
                     .colorFormat(WGPUTextureFormat_RGBA16Float)
                     .build();
        _threshPipeline = p.pipeline;
        _threshBGL = p.bgl(0);
    }
    {
        auto p = GpuPipelineBuilder(device)
                     .wgsl(BLOOM_DOWN_WGSL)
                     .bgl(singleTexBGL)
                     .colorFormat(WGPUTextureFormat_RGBA16Float)
                     .build();
        _downPipeline = p.pipeline;
        _downBGL = p.bgl(0);
    }
    {
        // Additive blend (one/one) so the upsample accumulates onto the
        // destination mip's existing downsampled content — the WebGPU
        // equivalent of GL's upsample glBlendFunc(GL_ONE, GL_ONE).
        // Bloom's up shader outputs alpha 1.0, so Blend::Additive (src.a·src +
        // 1·dst) is exactly the one/one accumulation GL uses.
        auto p = GpuPipelineBuilder(device)
                     .wgsl(BLOOM_UP_WGSL)
                     .bgl(singleTexBGL)
                     .colorFormat(WGPUTextureFormat_RGBA16Float)
                     .blend(GpuPipelineBuilder::Blend::Additive)
                     .build();
        _upPipeline = p.pipeline;
        _upBGL = p.bgl(0);
    }
    {
        // Composite renders back into sceneRT, whose pass carries a depth
        // attachment — declare a matching depth-transparent state (write=false,
        // Always) so the attachment states are compatible. Threshold/down/up
        // stay depthless: they target the standalone mip textures.
        auto p = GpuPipelineBuilder(device)
                     .wgsl(BLOOM_COMP_WGSL)
                     .bgl({ gpuUniform(0, wgsl_stage::frag, 16), gpuTexture(1), gpuTexture(2), gpuSampler(3) })
                     .colorFormat(WGPUTextureFormat_RGBA16Float)
                     .depth(false, WGPUCompareFunction_Always)
                     .multisample(sceneSampleCount)
                     .build();
        _compPipeline = p.pipeline;
        _compBGL = p.bgl(0);
    }
}

void BloomPass::_resizeGPU(int width, int height) {
    if (width == _gpuWidth && height == _gpuHeight && _gpuMips[0].tex) return;

    _destroyGPUTextures();

    _gpuWidth = width;
    _gpuHeight = height;

    auto makeTex = [this](int w, int h, WGPUTextureUsage usage) {
        WGPUTextureDescriptor d{};
        d.usage = usage;
        d.dimension = WGPUTextureDimension_2D;
        d.size = { (uint32_t)w, (uint32_t)h, 1 };
        d.format = WGPUTextureFormat_RGBA16Float;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        return wgpuDeviceCreateTexture(_gpuDevice, &d);
    };

    // Mip chain: level 0 is half-res, each subsequent level halves again —
    // the exact sizing of GL's InitMips loop.
    int w = width, h = height;
    for (int i = 0; i < MIP_LEVELS; ++i) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        _gpuMips[i].w = w;
        _gpuMips[i].h = h;
        _gpuMips[i].tex = makeTex(w, h, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
        _gpuMips[i].view = wgpuTextureCreateView(_gpuMips[i].tex, nullptr);
    }

    _snapshotTex = makeTex(width, height, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
    _snapshotView = wgpuTextureCreateView(_snapshotTex, nullptr);

    // texelSize per step is fixed for this resolution — written once here, not
    // every frame. Down step i samples mip[i-1]; up step i samples mip[i].
    struct StepUniforms {
        float tx, ty, filterRadius, pad;
    };
    for (int i = 1; i < MIP_LEVELS; ++i) {
        StepUniforms du{ 1.0f / (float)_gpuMips[i - 1].w, 1.0f / (float)_gpuMips[i - 1].h, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _downUniformBuf[i], 0, &du, sizeof(du));
        StepUniforms uu{ 1.0f / (float)_gpuMips[i].w, 1.0f / (float)_gpuMips[i].h, 1.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _upUniformBuf[i], 0, &uu, sizeof(uu));
    }

    auto makeSingleTexBG = [this](WGPUBindGroupLayout bgl, WGPUBuffer uniformBuf, WGPUTextureView view) {
        return GpuBindGroupBuilder(_gpuDevice, bgl)
            .buffer(0, uniformBuf, 16)
            .textureView(1, view)
            .sampler(2, _sampler)
            .build();
    };
    // Threshold samples the scene snapshot -> mip[0].
    _threshBG = makeSingleTexBG(_threshBGL, _threshUniformBuf, _snapshotView);
    for (int i = 1; i < MIP_LEVELS; ++i) {
        _downBG[i] = makeSingleTexBG(_downBGL, _downUniformBuf[i], _gpuMips[i - 1].view);// mip[i-1] -> mip[i]
        _upBG[i] = makeSingleTexBG(_upBGL, _upUniformBuf[i], _gpuMips[i].view);// mip[i]   -> mip[i-1]
    }

    _compBG = GpuBindGroupBuilder(_gpuDevice, _compBGL)
                  .buffer(0, _compUniformBuf, 16)
                  .textureView(1, _snapshotView)
                  .textureView(2, _gpuMips[0].view)// final accumulated bloom lands in mip[0]
                  .sampler(3, _sampler)
                  .build();
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

BloomPass::~BloomPass() {
    for (int i = 0; i < MIP_LEVELS; ++i) {
        if (_mips[i].tex) glDeleteTextures(1, &_mips[i].tex);
        if (_mips[i].fbo) glDeleteFramebuffers(1, &_mips[i].fbo);
    }
    if (_tempTex) glDeleteTextures(1, &_tempTex);
    if (_tempFBO) glDeleteFramebuffers(1, &_tempFBO);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    _destroyGPUTextures();
    if (_threshUniformBuf) wgpuBufferRelease(_threshUniformBuf);
    if (_compUniformBuf) wgpuBufferRelease(_compUniformBuf);
    for (int i = 0; i < MIP_LEVELS; ++i) {
        if (_downUniformBuf[i]) wgpuBufferRelease(_downUniformBuf[i]);
        if (_upUniformBuf[i]) wgpuBufferRelease(_upUniformBuf[i]);
    }
    if (_threshPipeline) wgpuRenderPipelineRelease(_threshPipeline);
    if (_downPipeline) wgpuRenderPipelineRelease(_downPipeline);
    if (_upPipeline) wgpuRenderPipelineRelease(_upPipeline);
    if (_compPipeline) wgpuRenderPipelineRelease(_compPipeline);
    if (_threshBGL) wgpuBindGroupLayoutRelease(_threshBGL);
    if (_downBGL) wgpuBindGroupLayoutRelease(_downBGL);
    if (_upBGL) wgpuBindGroupLayoutRelease(_upBGL);
    if (_compBGL) wgpuBindGroupLayoutRelease(_compBGL);
    if (_sampler) wgpuSamplerRelease(_sampler);
#endif
}

void BloomPass::InitMips(int w, int h) {
    if (_tempTex) glDeleteTextures(1, &_tempTex);
    if (_tempFBO) glDeleteFramebuffers(1, &_tempFBO);

    glGenTextures(1, &_tempTex);
    glBindTexture(GL_TEXTURE_2D, _tempTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_tempFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _tempFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _tempTex, 0);

    for (int i = 0; i < MIP_LEVELS; ++i) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        auto& m = _mips[i];
        m.w = w;
        m.h = h;

        if (m.tex) glDeleteTextures(1, &m.tex);
        if (m.fbo) glDeleteFramebuffers(1, &m.fbo);

        glGenTextures(1, &m.tex);
        glBindTexture(GL_TEXTURE_2D, m.tex);
#ifdef __EMSCRIPTEN__
        // WebGL 2.0 does not support GL_RGB16F as a color-renderable format. Must use GL_RGBA16F.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &m.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m.tex, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _initialized = true;
}

void BloomPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!enabled) return;
        if (!renderer.sceneRT) return;

        if (!_threshPipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, (uint32_t)renderer.sceneRT->GetNumSamples());
        }

        auto [w, h] = Window::Get()->GetPhysicalSize();
        _resizeGPU(w, h);

        auto* sceneRT = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
        WGPUTexture sceneTex = sceneRT->GetNativeTexture();
        if (!sceneTex) return;

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);

        // Snapshot sceneRT's current color contents before we start writing
        // into it again via the composite pass below (a texture cannot be
        // both a render attachment and a sampled binding in the same pass).
        WGPUTexelCopyTextureInfo copySrc{};
        copySrc.texture = sceneTex;
        copySrc.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo copyDst{};
        copyDst.texture = _snapshotTex;
        copyDst.aspect = WGPUTextureAspect_All;
        WGPUExtent3D copyExtent{ (uint32_t)_gpuWidth, (uint32_t)_gpuHeight, 1 };
        wgpuCommandEncoderCopyTextureToTexture(gpuEnc->encoder, &copySrc, &copyDst, &copyExtent);

        struct {
            float threshold, pad0, pad1, pad2;
        } threshU{ threshold, 0.0f, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _threshUniformBuf, 0, &threshU, sizeof(threshU));
        struct {
            float bloomStrength, pad0, pad1, pad2;
        } compU{ bloomStrength, 0.0f, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _compUniformBuf, 0, &compU, sizeof(compU));

        // loadOp=Clear for threshold/downsample (they own their whole target);
        // loadOp=Load for the additive upsample so the destination mip's
        // downsampled content is preserved and accumulated onto.
        auto runFullscreenPass =
            [&](WGPUTextureView target, WGPURenderPipeline pipeline, WGPUBindGroup bg, WGPULoadOp loadOp) {
                WGPURenderPassColorAttachment ca{};
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;// required for non-3D attachments
                ca.view = target;
                ca.loadOp = loadOp;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = { 0.0, 0.0, 0.0, 1.0 };
                WGPURenderPassDescriptor rpDesc{};
                rpDesc.colorAttachmentCount = 1;
                rpDesc.colorAttachments = &ca;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &rpDesc);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            };

        // 1. Threshold: snapshot -> mip[0].
        runFullscreenPass(_gpuMips[0].view, _threshPipeline, _threshBG, WGPULoadOp_Clear);
        // 2. Downsample chain: mip[i-1] -> mip[i].
        for (int i = 1; i < MIP_LEVELS; ++i)
            runFullscreenPass(_gpuMips[i].view, _downPipeline, _downBG[i], WGPULoadOp_Clear);
        // 3. Upsample chain (additive): mip[i] -> mip[i-1], accumulating.
        for (int i = MIP_LEVELS - 1; i >= 1; --i)
            runFullscreenPass(_gpuMips[i - 1].view, _upPipeline, _upBG[i], WGPULoadOp_Load);

        // 4. Composite: snapshot + final bloom (mip[0]) -> sceneRT (loadOp=Load,
        // since earlier passes this frame have already drawn into sceneRT).
        renderer.sceneRT->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _compPipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _compBG, 0, nullptr);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 3, 1, 0, 0);
        renderer.sceneRT->End();
        return;
    }
#endif
    if (!enabled) return;
    if (!renderer.msaaResolveRT) return;
    if (renderer.gl.screenQuadVAO == 0) return;

    auto [w, h] = Window::Get()->GetPhysicalSize();
    if (!_initialized || w != _lastW || h != _lastH) {
        InitMips(w, h);
        _lastW = w;
        _lastH = h;
    }

    auto sceneTexture = static_cast<GLuint>(renderer.msaaResolveRT->GetTextureID());

    ShaderProgram* threshShader = AssetManager::Get().GetShader("bloom_threshold");
    ShaderProgram* downShader = AssetManager::Get().GetShader("bloom_downsample");
    ShaderProgram* upShader = AssetManager::Get().GetShader("bloom_upsample");
    ShaderProgram* compShader = AssetManager::Get().GetShader("bloom_composite");
    if (!threshShader || !downShader || !upShader || !compShader) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);// fullscreen quads — never wireframe
#endif

    // 1. Threshold pass → mip[0]
    {
        glBindFramebuffer(GL_FRAMEBUFFER, _mips[0].fbo);
        glViewport(0, 0, _mips[0].w, _mips[0].h);
        threshShader->Activate();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        threshShader->SetUniform("u_scene", 0);
        threshShader->SetUniform("u_threshold", threshold);
        DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
        threshShader->Deactivate();
    }

    // 2. Downsample chain: mip[i-1] → mip[i]
    downShader->Activate();
    for (int i = 1; i < MIP_LEVELS; ++i) {
        auto& src = _mips[i - 1];
        auto& dst = _mips[i];
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.w, dst.h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src.tex);
        downShader->SetUniform("u_src", 0);
        downShader->SetUniform("u_texelSize", glm::vec2(1.0f / src.w, 1.0f / src.h));
        DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
    }
    downShader->Deactivate();

    // 3. Upsample chain: mip[i] → mip[i-1]  (additive blend)
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    upShader->Activate();
    for (int i = MIP_LEVELS - 1; i >= 1; --i) {
        auto& src = _mips[i];
        auto& dst = _mips[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.w, dst.h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src.tex);
        upShader->SetUniform("u_src", 0);
        upShader->SetUniform("u_texelSize", glm::vec2(1.0f / src.w, 1.0f / src.h));
        upShader->SetUniform("u_filterRadius", 1.0f);
        DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
    }
    upShader->Deactivate();
    glDisable(GL_BLEND);

    // 4. Composite: scene + bloom → back into msaaResolveRT (linear HDR, no tonemapping)
    // PostProcessPass reads msaaResolveRT and applies HDR tonemapping as the final step.
    GLuint resolveTargetFBO = static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID();
    glBindFramebuffer(GL_FRAMEBUFFER, _tempFBO);
    glViewport(0, 0, w, h);
    compShader->Activate();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _mips[0].tex);
    compShader->SetUniform("u_scene", 0);
    compShader->SetUniform("u_bloom", 1);
    compShader->SetUniform("u_bloomStrength", bloomStrength);
    DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
    compShader->Deactivate();

    // Unbind composite textures to prevent any feedback loops during blitting/subsequent draws
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Blit from temp framebuffer to resolveTargetFBO to avoid GPU feedback loop
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _tempFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveTargetFBO);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveTargetFBO);

    glEnable(GL_DEPTH_TEST);
}

#include "renderer.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "gfx_factory.hpp"
#include "graphics_server.hpp"
#include "light_component.hpp"
#include "sun_component.hpp"
#include "mesh.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "window.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "command_encoder.hpp"
#include "voxel_chunk_pass_wgsl.hpp"
#endif

// ---- Helper: build/draw the full-screen quad ---------------------------------
static void DrawScreenQuadVAO(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}


// ============================================================================
//  SunPass  (billboard quad at light direction, HDR gold for bloom glow)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
SunPass::~SunPass() {
    if (_uniformBG)  wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline)   wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf)  wgpuBufferRelease(_vertexBuf);
}

void SunPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue  = queue;
    {
        WGPUBufferDescriptor d{};
        d.size  = sizeof(SUN_QUAD_VERTS);
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        _vertexBuf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, _vertexBuf, 0, SUN_QUAD_VERTS, sizeof(SUN_QUAD_VERTS));
    }
    {
        WGPUBufferDescriptor d{};
        d.size  = 192; // model(64) + viewProj(64) + color(16) + fogColor(16) + fogDensity(16) + cameraPos(16)
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    auto p = GpuPipelineBuilder(device)
        .wgsl(SUN_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::both, 192) })
        .vertex(12, { {WGPUVertexFormat_Float32x3, 0, 0} })
        .colorFormat(colorFormat).depth(true, WGPUCompareFunction_Less)
        .multisample(sampleCount).build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _uniformBG  = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 192).build();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void SunPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float,
                     (uint32_t)renderer.sceneRT->GetNumSamples());
        }
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();
        SunComponent*   sun   = ctx->GetMainSun();
        if (!sun) return;

        glm::vec3 lightDir = light ? glm::normalize(light->direction) : glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f));
        const float CHUNK_SIZE = 32.0f;
        const float WORLD_W    = 25.0f;
        const float WORLD_D    = 25.0f;
        const float CENTER_X   = WORLD_W * CHUNK_SIZE * 0.5f;
        const float CENTER_Z   = WORLD_D * CHUNK_SIZE * 0.5f;

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
            if (lightDir.x > 0.0f)      sunPos.x = CENTER_X - CHUNK_SIZE * WORLD_W;
            else if (lightDir.x < 0.0f) sunPos.x = CENTER_X + CHUNK_SIZE * WORLD_W;
            else                         sunPos.y = sun->height * 2.0f;
        }

        glm::vec3 camPos   = camera->GetEyePosition();
        glm::vec3 toCamera = glm::normalize(camPos - sunPos);
        glm::vec3 right    = glm::normalize(glm::cross(toCamera, glm::vec3(0, 1, 0)));
        glm::vec3 up       = glm::cross(right, toCamera);

        glm::mat4 model(1.0f);
        model[0] = glm::vec4(right * sun->billboardRadius, 0);
        model[1] = glm::vec4(up    * sun->billboardRadius, 0);
        model[2] = glm::vec4(toCamera,                     0);
        model[3] = glm::vec4(sunPos,                       1);

        glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        struct {
            glm::mat4 model;
            glm::mat4 viewProj;
            glm::vec4 color;
            glm::vec4 fogColor;
            glm::vec4 fogDensity;
            glm::vec4 cameraPos;
        } u{
            model, viewProj,
            glm::vec4(sun->billboardColor, 1.0f),
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), // VX: COLOR_WHITE
            glm::vec4(0.000003f, 0.0f, 0.0f, 0.0f),
            glm::vec4(camPos, 1.0f),
        };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt     = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
        rt->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(gpuEnc->pass, 0, _vertexBuf, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 6, 1, 0, 0);
        rt->End();
        return;
    }
#endif
    if (renderer.gl.skyboxVAO == 0) return; // skybox cube doubles as bounding check

    ShaderProgram* shader = AssetManager::Get().GetShader("sun");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent*  light  = ctx->GetMainLight();
    SunComponent*    sun    = ctx->GetMainSun();
    if (!sun) return;

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    // Mirror VX sun.py get_model_matrix(): push sun to world edge along light direction
    glm::vec3 lightDir = light ? glm::normalize(light->direction) : glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f));
    const float CHUNK_SIZE = 32.0f;
    const float WORLD_W    = 25.0f;
    const float WORLD_D    = 25.0f;
    const float CENTER_X   = WORLD_W * CHUNK_SIZE * 0.5f;
    const float CENTER_Z   = WORLD_D * CHUNK_SIZE * 0.5f;

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
        if (lightDir.x > 0.0f)      sunPos.x = CENTER_X - CHUNK_SIZE * WORLD_W;
        else if (lightDir.x < 0.0f) sunPos.x = CENTER_X + CHUNK_SIZE * WORLD_W;
        else                         sunPos.y = sun->height * 2.0f;
    }

    // Billboard: orient quad to face the camera
    glm::vec3 camPos   = camera->GetEyePosition();
    glm::vec3 toCamera = glm::normalize(camPos - sunPos);
    glm::vec3 right    = glm::normalize(glm::cross(toCamera, glm::vec3(0, 1, 0)));
    glm::vec3 up       = glm::cross(right, toCamera);

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * sun->billboardRadius, 0);
    model[1] = glm::vec4(up    * sun->billboardRadius, 0);
    model[2] = glm::vec4(toCamera,                     0);
    model[3] = glm::vec4(sunPos,                       1);

    glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();

    shader->Activate();
    shader->SetUniform("u_model",      model);
    shader->SetUniform("u_viewProj",   viewProj);
    shader->SetUniform("u_color",      sun->billboardColor);
    shader->SetUniform("u_fogColor",   glm::vec3(1.0f, 1.0f, 1.0f)); // VX: COLOR_WHITE
    shader->SetUniform("u_fogDensity", 0.000003f);                    // VX: 0.000003

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // billboard quad — never wireframe
#endif

    // Simple quad: two triangles from the skybox cube's first face vertices
    // Use a minimal inline quad VAO via screenQuadVAO trick — draw a unit quad
    static GLuint sunVAO = 0, sunVBO = 0;
    if (sunVAO == 0) {
        float q[] = { -1,-1,0,  1,-1,0,  1,1,0,  -1,-1,0,  1,1,0,  -1,1,0 };
        glGenVertexArrays(1, &sunVAO);
        glGenBuffers(1, &sunVBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }
    glBindVertexArray(sunVAO);
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
    if (_uniformBG)  wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline)   wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf)  wgpuBufferRelease(_vertexBuf);
}

void SkyboxPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue  = queue;
    {
        WGPUBufferDescriptor d{};
        d.size  = sizeof(SKYBOX_CUBE_VERTS);
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        _vertexBuf = wgpuDeviceCreateBuffer(device, &d);
        wgpuQueueWriteBuffer(queue, _vertexBuf, 0, SKYBOX_CUBE_VERTS, sizeof(SKYBOX_CUBE_VERTS));
    }
    {
        WGPUBufferDescriptor d{};
        d.size  = 96; // viewProj(64) + skyColor(16) + horizonColor(16)
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Sky is rendered after opaque geometry at z=far; LessEqual+no-write lets
    // it fill empty pixels without overwriting opaque geometry depth.
    auto p = GpuPipelineBuilder(device)
        .wgsl(SKYBOX_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::both, 96) })
        .vertex(12, { {WGPUVertexFormat_Float32x3, 0, 0} })
        .colorFormat(colorFormat).depth(false, WGPUCompareFunction_LessEqual)
        .multisample(sampleCount).build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _uniformBG  = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 96).build();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void SkyboxPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float,
                     (uint32_t)renderer.sceneRT->GetNumSamples());
        }
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;

        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(camera->GetViewMatrix()));
        glm::mat4 viewProj = camera->GetProjectionMatrix() * viewNoTranslation;

        struct {
            glm::mat4 viewProj;
            glm::vec4 skyColor;
            glm::vec4 horizonColor;
        } u{ viewProj, glm::vec4(skyColor, 1.0f), glm::vec4(horizonColor, 1.0f) };
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt     = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
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

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    shader->Activate();

    // Strip translation from view matrix so sky doesn't move with camera
    glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(camera->GetViewMatrix()));
    shader->SetUniform("u_proj",         camera->GetProjectionMatrix());
    shader->SetUniform("u_view",         viewNoTranslation);
    shader->SetUniform("u_skyColor",     skyColor);
    shader->SetUniform("u_horizonColor", horizonColor);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fullscreen gradient — never wireframe
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
    if (_uniformBG)       wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL)      wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline)        wgpuRenderPipelineRelease(_pipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf)  wgpuBufferRelease(_drawUniformBuf);
}

void VoxelChunkPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue  = queue;
    {
        WGPUBufferDescriptor d{};
        d.size  = VOXEL_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // VoxelVertex: x,y,z,voxel_id + face_id,pad[3] — two Uint8x4 attrs since
    // WebGPU has no Uint8x3 / plain uint8 format.
    auto p = GpuPipelineBuilder(device)
        .wgsl(VOXEL_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::both, VOXEL_FRAME_UNIFORM_SIZE),
               gpuDynUniform(1, wgsl_stage::vert, VOXEL_DRAW_UNIFORM_SIZE) })
        .vertex(8, { {WGPUVertexFormat_Uint8x4, 0, 0}, {WGPUVertexFormat_Uint8x4, 4, 1} })
        .colorFormat(colorFormat).depth(true, WGPUCompareFunction_Less)
        .cull(WGPUCullMode_Back).multisample(sampleCount).build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
}

void VoxelChunkPass::_ensureDrawCapacity(uint32_t drawCount) {
    if (drawCount <= _drawSlotCapacity && _drawUniformBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(drawCount, std::max<uint32_t>(_drawSlotCapacity * 2, 16));

    if (_drawUniformBuf) { wgpuBufferRelease(_drawUniformBuf); _drawUniformBuf = nullptr; }
    if (_uniformBG)      { wgpuBindGroupRelease(_uniformBG);   _uniformBG      = nullptr; }

    WGPUBufferDescriptor d{};
    d.size  = static_cast<uint64_t>(newCapacity) * VOXEL_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf   = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
        .buffer(0, _frameUniformBuf, VOXEL_FRAME_UNIFORM_SIZE)
        .buffer(1, _drawUniformBuf,  VOXEL_DRAW_UNIFORM_SIZE)
        .build();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void VoxelChunkPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
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
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float,
                     (uint32_t)renderer.sceneRT->GetNumSamples());
        }

        struct DrawItem { Buffer* buf; glm::mat4 model; };
        std::vector<DrawItem> draws;
        draws.reserve(gpuQueueCmds.size());
        for (const auto& sortable : gpuQueueCmds) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
            if (!mesh || mesh->type != MeshType::VOXEL) continue;
            if (!mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            draws.push_back({ buf, cmd.transform });
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir   = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse  : glm::vec3(1.0f);
        glm::vec3 ambient    = light ? light->ambient * 0.15f : glm::vec3(0.1f);
        glm::mat4 viewProj   = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        struct {
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 ambient;
            glm::vec4 fogColor;
            glm::vec4 fogDensity;
        } frameUniforms{
            viewProj,
            glm::vec4(camera->GetEyePosition(), 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(ambient, 0.0f),
            glm::vec4(0.55f, 0.65f, 0.75f, 1.0f),
            glm::vec4(0.003f, 0.0f, 0.0f, 0.0f),
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
        auto* rt     = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
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
    LightComponent*  light  = ctx->GetMainLight();

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    // Render into the same target as ForwardOpaquePass (no clear — already done)
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    shader->Activate();

    glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    shader->SetUniform("u_viewProj",    viewProj);
    shader->SetUniform("u_cameraPos",   camera->GetEyePosition());

    glm::vec3 lightDir   = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
    glm::vec3 lightColor = light ? light->diffuse  : glm::vec3(1.0f);
    glm::vec3 ambient    = light ? light->ambient * 0.15f : glm::vec3(0.1f);
    shader->SetUniform("u_lightDir",    lightDir);
    shader->SetUniform("u_lightColor",  lightColor);
    shader->SetUniform("u_ambientColor",ambient);
    // VX ties terrain/water fog color to the skybox's sky gradient color every frame.
    SkyboxPass* skybox = renderer.GetPass<SkyboxPass>();
    shader->SetUniform("u_fogColor",     skybox ? skybox->skyColor : glm::vec3(0.686f, 0.933f, 0.933f));
    shader->SetUniform("u_fogDensity",   0.00001f); // VX: scene.py u_fog_density
    shader->SetUniform("u_paletteIndex", paletteIndex);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    // Opt into wireframe explicitly; don't rely on GL_LINE leaking from
    // ForwardOpaquePass, since upstream passes now reset polygon mode themselves.
    glPolygonMode(GL_FRONT_AND_BACK, renderer.wireframeEnabled ? GL_LINE : GL_FILL);
#endif

    for (const auto& sortable : queue) {
        const auto& cmd = sortable.cmd;
        Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);

        if (!mesh || mesh->type != MeshType::VOXEL) continue;
        if (!mesh->UsesRenderMesh()) continue;

        shader->SetUniform("u_model", cmd.transform);

        Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
        if (buf && buf->IsInitialized() && buf->GetVertexCount() > 0) {
            buf->Draw(nullptr, PrimitiveTopology::Triangles);
        }
    }

    shader->Deactivate();
}

// ============================================================================
//  WaterPass
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
WaterPass::~WaterPass() {
    if (_uniformBG)       wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL)      wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline)        wgpuRenderPipelineRelease(_pipeline);
    if (_frameUniformBuf) wgpuBufferRelease(_frameUniformBuf);
    if (_drawUniformBuf)  wgpuBufferRelease(_drawUniformBuf);
}

void WaterPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue  = queue;
    {
        WGPUBufferDescriptor d{};
        d.size  = WATER_FRAME_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _frameUniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    // Depth-tested, no depth write — water is occluded by opaque geometry but
    // never occludes itself/other transparents (mirrors GL glDepthMask(GL_FALSE)).
    auto p = GpuPipelineBuilder(device)
        .wgsl(WATER_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::both, WATER_FRAME_UNIFORM_SIZE),
               gpuDynUniform(1, wgsl_stage::both, WATER_DRAW_UNIFORM_SIZE) })
        .vertex(56, { {WGPUVertexFormat_Float32x3,  0, 0},
                      {WGPUVertexFormat_Float32x2, 12, 1},
                      {WGPUVertexFormat_Float32x3, 20, 2} })
        .colorFormat(colorFormat).blend().depth(false, WGPUCompareFunction_LessEqual)
        .multisample(sampleCount).build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
}

void WaterPass::_ensureDrawCapacity(uint32_t drawCount) {
    if (drawCount <= _drawSlotCapacity && _drawUniformBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(drawCount, std::max<uint32_t>(_drawSlotCapacity * 2, 16));

    if (_drawUniformBuf) { wgpuBufferRelease(_drawUniformBuf); _drawUniformBuf = nullptr; }
    if (_uniformBG)      { wgpuBindGroupRelease(_uniformBG);   _uniformBG      = nullptr; }

    WGPUBufferDescriptor d{};
    d.size  = static_cast<uint64_t>(newCapacity) * WATER_DRAW_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _drawUniformBuf   = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _drawSlotCapacity = newCapacity;

    _uniformBG = GpuBindGroupBuilder(_gpuDevice, _uniformBGL)
        .buffer(0, _frameUniformBuf, WATER_FRAME_UNIFORM_SIZE)
        .buffer(1, _drawUniformBuf,  WATER_DRAW_UNIFORM_SIZE)
        .build();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

void WaterPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        const auto& queue = renderer.GetTransparentQueue();
        if (queue.empty())     return;
        if (!renderer.sceneRT) return;

        CameraComponent* camera = ctx->GetMainCamera();
        if (!camera) return;
        LightComponent* light = ctx->GetMainLight();

        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float,
                     (uint32_t)renderer.sceneRT->GetNumSamples());
        }

        // Water params now live on WaterMaterial (mesh/material decoupling);
        // fall back to WaterMaterial's own defaults when the mesh carries a
        // plain Material, mirroring the GL path's `wm ? ... : default` chain.
        struct DrawItem { Buffer* buf; glm::mat4 model; WaterMaterial wd; };
        std::vector<DrawItem> draws;
        draws.reserve(queue.size());
        for (const auto& sortable : queue) {
            const auto& cmd = sortable.cmd;
            Mesh* mesh = AssetManager::Get().GetMeshPtr(cmd.mesh);
            if (!mesh || mesh->type != MeshType::PRIM) continue;
            Material* mat = mesh->GetMaterial();
            if (!mat || mat->renderQueue != RenderQueue::Transparent) continue;
            if (!mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;
            auto* wm = dynamic_cast<WaterMaterial*>(mat);
            draws.push_back({ buf, cmd.transform, wm ? *wm : WaterMaterial{} });
        }
        if (draws.empty()) return;

        _ensureDrawCapacity(static_cast<uint32_t>(draws.size()));

        glm::vec3 lightDir   = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
        glm::vec3 lightColor = light ? light->diffuse : glm::vec3(1.0f);
        glm::mat4 viewProj   = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        struct {
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 lightDir;
            glm::vec4 lightColor;
            glm::vec4 time;
        } frameUniforms{
            viewProj,
            glm::vec4(camera->GetEyePosition(), 1.0f),
            glm::vec4(lightDir, 0.0f),
            glm::vec4(lightColor, 0.0f),
            glm::vec4(renderer.frameTime, 0.0f, 0.0f, 0.0f),
        };
        wgpuQueueWriteBuffer(_gpuQueue, _frameUniformBuf, 0, &frameUniforms, sizeof(frameUniforms));

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
            std::memcpy(slot + 64, &params0,  sizeof(glm::vec4));
            std::memcpy(slot + 80, &fogColor, sizeof(glm::vec4));
        }
        wgpuQueueWriteBuffer(_gpuQueue, _drawUniformBuf, 0, drawData.data(), drawData.size());

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        renderer.sceneRT->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        for (size_t i = 0; i < draws.size(); ++i) {
            uint32_t dynamicOffset = static_cast<uint32_t>(i * WATER_DRAW_SLOT_STRIDE);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 1, &dynamicOffset);
            draws[i].buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        renderer.sceneRT->End();
        return;
    }
#endif
    const auto& queue = renderer.GetTransparentQueue();
    if (queue.empty()) return;

    ShaderProgram* shader = AssetManager::Get().GetShader("water");
    if (!shader) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent*  light  = ctx->GetMainLight();

    auto [width, height] = Window::Get()->GetPhysicalSize();
    glViewport(0, 0, width, height);

    // Render into msaaResolveRT so water goes through bloom + tonemapping
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.msaaResolveRT.get())->GetNativeFBOID());

    // WaterPass runs after MSAAResolvePass so the resolved depth is ready
    GLuint depthTex = renderer.GetResolvedDepthTexture();

    shader->Activate();

    glm::mat4 proj    = camera->GetProjectionMatrix();
    glm::mat4 view    = camera->GetViewMatrix();
    glm::mat4 viewProj = proj * view;
    shader->SetUniform("u_viewProj",     viewProj);
    shader->SetUniform("u_cameraPos",    camera->GetEyePosition());
    shader->SetUniform("u_time",         renderer.frameTime);
    shader->SetUniform("u_invProj",      glm::inverse(proj));
    shader->SetUniform("u_invView",      glm::inverse(view));
    shader->SetUniform("u_screenSize",   glm::vec2((float)width, (float)height));
    shader->SetUniform("u_depthTexture", 1);

    glm::vec3 lightDir   = light ? glm::normalize(-light->direction) : glm::vec3(0.5f, 1.0f, 0.3f);
    glm::vec3 lightColor = light ? light->diffuse  : glm::vec3(1.0f);
    shader->SetUniform("u_lightDir",   lightDir);
    shader->SetUniform("u_lightColor", lightColor);

    // VX ties terrain/water fog color to the skybox's sky gradient color every frame.
    SkyboxPass* skybox = renderer.GetPass<SkyboxPass>();
    glm::vec3 skyFogColor = skybox ? skybox->skyColor : glm::vec3(0.686f, 0.933f, 0.933f);

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

        Material* mat = mesh->GetMaterial();
        if (!mat || mat->renderQueue != RenderQueue::Transparent) continue;

        auto* wm = dynamic_cast<WaterMaterial*>(mesh->GetMaterial());
        shader->SetUniform("u_waterLine",    wm ? wm->waterLine       : 32.0f);
        shader->SetUniform("u_waveStrength", wm ? wm->waveStrength    :  0.1f);
        shader->SetUniform("u_waveSpeed",    wm ? wm->waveSpeed       :  1.0f);
        shader->SetUniform("u_fogColor",     skyFogColor);
        shader->SetUniform("u_fogDensity",   wm ? wm->waterFogDensity :  0.00001f);
        shader->SetUniform("u_deepColor",    wm ? wm->deepColor       : glm::vec3{0.05f, 0.1f, 0.25f});
        shader->SetUniform("u_shallowColor", wm ? wm->shallowColor    : glm::vec3{0.686f, 0.933f, 0.933f});
        shader->SetUniform("u_beerCoef",     wm ? wm->beerCoef        :  0.095f);
        shader->SetUniform("u_model",        cmd.transform);

        glBindVertexArray(mesh->vao);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(mesh->triCount * 3),
                       GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    shader->Deactivate();
}

// ============================================================================
//  BloomPass  (pyramid downsample + upsample + ACES composite)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
void BloomPass::_destroyGPUTextures() {
    if (_blurHBG)     { wgpuBindGroupRelease(_blurHBG);      _blurHBG     = nullptr; }
    if (_blurVBG)     { wgpuBindGroupRelease(_blurVBG);      _blurVBG     = nullptr; }
    if (_compBG)      { wgpuBindGroupRelease(_compBG);       _compBG      = nullptr; }
    if (_brightAView) { wgpuTextureViewRelease(_brightAView); _brightAView = nullptr; }
    if (_brightBView) { wgpuTextureViewRelease(_brightBView); _brightBView = nullptr; }
    if (_snapshotView){ wgpuTextureViewRelease(_snapshotView);_snapshotView= nullptr; }
    if (_brightA)     { wgpuTextureRelease(_brightA);        _brightA     = nullptr; }
    if (_brightB)     { wgpuTextureRelease(_brightB);        _brightB     = nullptr; }
    if (_snapshotTex) { wgpuTextureRelease(_snapshotTex);    _snapshotTex = nullptr; }
}

void BloomPass::_initGPU(WGPUDevice device, WGPUQueue queue, uint32_t sceneSampleCount) {
    _gpuDevice = device;
    _gpuQueue  = queue;

    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    auto makeUniformBuf = [device](uint64_t size) {
        WGPUBufferDescriptor d{};
        d.size  = size;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        return wgpuDeviceCreateBuffer(device, &d);
    };
    _threshUniformBuf = makeUniformBuf(16);
    _blurHUniformBuf  = makeUniformBuf(16);
    _blurVUniformBuf  = makeUniformBuf(16);
    _compUniformBuf   = makeUniformBuf(16);

    // Thresh and blur share the same BGL shape: 1 uniform + 1 texture + 1 sampler.
    std::vector<GpuBGLEntry> singleTexBGL = {
        gpuUniform(0, wgsl_stage::frag, 16),
        gpuTexture(1), gpuSampler(2),
    };
    {
        auto p = GpuPipelineBuilder(device).wgsl(BLOOM_THRESH_WGSL)
            .bgl(singleTexBGL).colorFormat(WGPUTextureFormat_RGBA16Float).build();
        _threshPipeline = p.pipeline; _threshBGL = p.bgl(0);
    }
    {
        auto p = GpuPipelineBuilder(device).wgsl(BLOOM_BLUR_WGSL)
            .bgl(singleTexBGL).colorFormat(WGPUTextureFormat_RGBA16Float).build();
        _blurPipeline = p.pipeline; _blurBGL = p.bgl(0);
    }
    {
        // Composite renders back into sceneRT, whose pass carries a depth
        // attachment — declare a matching depth-transparent state (write=false,
        // Always) so the attachment states are compatible. Threshold/blur
        // stay depthless: they target the standalone half-res bright textures.
        auto p = GpuPipelineBuilder(device).wgsl(BLOOM_COMP_WGSL)
            .bgl({ gpuUniform(0, wgsl_stage::frag, 16),
                   gpuTexture(1), gpuTexture(2), gpuSampler(3) })
            .colorFormat(WGPUTextureFormat_RGBA16Float)
            .depth(false, WGPUCompareFunction_Always)
            .multisample(sceneSampleCount)
            .build();
        _compPipeline = p.pipeline; _compBGL = p.bgl(0);
    }
}

void BloomPass::_resizeGPU(int width, int height) {
    if (width == _gpuWidth && height == _gpuHeight && _brightA) return;

    _destroyGPUTextures();

    _gpuWidth  = width;
    _gpuHeight = height;
    _gpuHalfW  = std::max(1, width / 2);
    _gpuHalfH  = std::max(1, height / 2);

    auto makeTex = [this](int w, int h, WGPUTextureUsage usage) {
        WGPUTextureDescriptor d{};
        d.usage         = usage;
        d.dimension     = WGPUTextureDimension_2D;
        d.size          = { (uint32_t)w, (uint32_t)h, 1 };
        d.format        = WGPUTextureFormat_RGBA16Float;
        d.mipLevelCount = 1;
        d.sampleCount   = 1;
        return wgpuDeviceCreateTexture(_gpuDevice, &d);
    };
    _brightA     = makeTex(_gpuHalfW, _gpuHalfH, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
    _brightB     = makeTex(_gpuHalfW, _gpuHalfH, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
    _snapshotTex = makeTex(_gpuWidth, _gpuHeight, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);

    _brightAView  = wgpuTextureCreateView(_brightA, nullptr);
    _brightBView  = wgpuTextureCreateView(_brightB, nullptr);
    _snapshotView = wgpuTextureCreateView(_snapshotTex, nullptr);

    // Direction+texelSize are fixed for this resolution — written once here,
    // not every frame (see header comment).
    struct BlurUniforms { float dx, dy, tx, ty; };
    BlurUniforms hU{ 1.0f, 0.0f, 1.0f / (float)_gpuHalfW, 1.0f / (float)_gpuHalfH };
    BlurUniforms vU{ 0.0f, 1.0f, 1.0f / (float)_gpuHalfW, 1.0f / (float)_gpuHalfH };
    wgpuQueueWriteBuffer(_gpuQueue, _blurHUniformBuf, 0, &hU, sizeof(hU));
    wgpuQueueWriteBuffer(_gpuQueue, _blurVUniformBuf, 0, &vU, sizeof(vU));

    auto makeSingleTexBG = [this](WGPUBindGroupLayout bgl, WGPUBuffer uniformBuf, WGPUTextureView view) {
        return GpuBindGroupBuilder(_gpuDevice, bgl)
            .buffer(0, uniformBuf, 16)
            .textureView(1, view)
            .sampler(2, _sampler)
            .build();
    };
    _blurHBG = makeSingleTexBG(_blurBGL, _blurHUniformBuf, _brightAView); // thresholded -> blurred-H
    _blurVBG = makeSingleTexBG(_blurBGL, _blurVUniformBuf, _brightBView); // blurred-H   -> blurred-HV (final, lands in _brightA)

    _compBG = GpuBindGroupBuilder(_gpuDevice, _compBGL)
        .buffer(0, _compUniformBuf, 16)
        .textureView(1, _snapshotView)
        .textureView(2, _brightAView) // final blurred bloom (H then V both land here)
        .sampler(3, _sampler)
        .build();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

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
    if (_blurHUniformBuf)  wgpuBufferRelease(_blurHUniformBuf);
    if (_blurVUniformBuf)  wgpuBufferRelease(_blurVUniformBuf);
    if (_compUniformBuf)   wgpuBufferRelease(_compUniformBuf);
    if (_threshPipeline)   wgpuRenderPipelineRelease(_threshPipeline);
    if (_blurPipeline)     wgpuRenderPipelineRelease(_blurPipeline);
    if (_compPipeline)     wgpuRenderPipelineRelease(_compPipeline);
    if (_threshBGL)        wgpuBindGroupLayoutRelease(_threshBGL);
    if (_blurBGL)          wgpuBindGroupLayoutRelease(_blurBGL);
    if (_compBGL)          wgpuBindGroupLayoutRelease(_compBGL);
    if (_sampler)          wgpuSamplerRelease(_sampler);
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
        m.w = w; m.h = h;

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

void BloomPass::Execute(GraphicsServer* ctx, Renderer& renderer, CommandEncoder* enc) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!enabled) return;
        if (!renderer.sceneRT) return;

        if (!_threshPipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
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
        copySrc.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo copyDst{};
        copyDst.texture = _snapshotTex;
        copyDst.aspect  = WGPUTextureAspect_All;
        WGPUExtent3D copyExtent{ (uint32_t)_gpuWidth, (uint32_t)_gpuHeight, 1 };
        wgpuCommandEncoderCopyTextureToTexture(gpuEnc->encoder, &copySrc, &copyDst, &copyExtent);

        struct { float threshold, pad0, pad1, pad2; } threshU{ threshold, 0.0f, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _threshUniformBuf, 0, &threshU, sizeof(threshU));
        struct { float bloomStrength, pad0, pad1, pad2; } compU{ bloomStrength, 0.0f, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(_gpuQueue, _compUniformBuf, 0, &compU, sizeof(compU));

        // Threshold pass's source bind group reads sceneRT's live texture, so
        // it is rebuilt fresh each frame (uncached) rather than stored as a
        // class member.
        WGPUBindGroup threshBG = GpuBindGroupBuilder(_gpuDevice, _threshBGL)
            .buffer(0, _threshUniformBuf, 16)
            .texture(1, sceneTex)
            .sampler(2, _sampler)
            .build();

        auto runFullscreenPass = [&](WGPUTextureView target, WGPURenderPipeline pipeline, WGPUBindGroup bg) {
            WGPURenderPassColorAttachment ca{};
            ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // required for non-3D attachments
            ca.view       = target;
            ca.loadOp     = WGPULoadOp_Clear;
            ca.storeOp    = WGPUStoreOp_Store;
            ca.clearValue = { 0.0, 0.0, 0.0, 1.0 };
            WGPURenderPassDescriptor rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments     = &ca;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &rpDesc);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        };

        runFullscreenPass(_brightAView, _threshPipeline, threshBG); // threshold -> _brightA
        runFullscreenPass(_brightBView, _blurPipeline,   _blurHBG); // blur H: _brightA -> _brightB
        runFullscreenPass(_brightAView, _blurPipeline,   _blurVBG); // blur V: _brightB -> _brightA (final)

        // Composite: snapshot + final blurred bloom -> sceneRT (loadOp=Load,
        // since earlier passes this frame have already drawn into sceneRT).
        renderer.sceneRT->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _compPipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _compBG, 0, nullptr);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 3, 1, 0, 0);
        renderer.sceneRT->End();

        wgpuBindGroupRelease(threshBG);
        return;
    }
#endif
    if (!enabled)                     return;
    if (!renderer.msaaResolveRT)      return;
    if (renderer.gl.screenQuadVAO == 0)  return;

    auto [w, h] = Window::Get()->GetPhysicalSize();
    if (!_initialized || w != _lastW || h != _lastH) {
        InitMips(w, h);
        _lastW = w; _lastH = h;
    }

    GLuint sceneTexture = static_cast<GLuint>(renderer.msaaResolveRT->GetTextureID());

    ShaderProgram* threshShader  = AssetManager::Get().GetShader("bloom_threshold");
    ShaderProgram* downShader    = AssetManager::Get().GetShader("bloom_downsample");
    ShaderProgram* upShader      = AssetManager::Get().GetShader("bloom_upsample");
    ShaderProgram* compShader    = AssetManager::Get().GetShader("bloom_composite");
    if (!threshShader || !downShader || !upShader || !compShader) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fullscreen quads — never wireframe
#endif

    // 1. Threshold pass → mip[0]
    {
        glBindFramebuffer(GL_FRAMEBUFFER, _mips[0].fbo);
        glViewport(0, 0, _mips[0].w, _mips[0].h);
        threshShader->Activate();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        threshShader->SetUniform("u_scene",     0);
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
        downShader->SetUniform("u_src",       0);
        downShader->SetUniform("u_texelSize",  glm::vec2(1.0f / src.w, 1.0f / src.h));
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
        upShader->SetUniform("u_src",          0);
        upShader->SetUniform("u_texelSize",     glm::vec2(1.0f / src.w, 1.0f / src.h));
        upShader->SetUniform("u_filterRadius",  1.0f);
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
    compShader->SetUniform("u_scene",        0);
    compShader->SetUniform("u_bloom",        1);
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

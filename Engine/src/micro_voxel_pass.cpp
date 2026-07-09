#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "console_subsystem.hpp"
#include "gfx_factory.hpp"
#include "gl_render_target.hpp"
#include "graphics_subsystem.hpp"
#include "light_component.hpp"
#include "renderer.hpp"
#include "voxel_volume_component.hpp"
#include "window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "command_encoder.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "micro_voxel_pass_wgsl.hpp"
#endif

// ============================================================================
//  MicroVoxelPass — raymarched micro voxels (experimental)
// ============================================================================
// Renders a dense voxel volume (10 cm voxels by default) with a fullscreen
// two-level DDA raymarch: a coarse pass strides brick-by-brick, skipping
// empty bricks via the occupancy texture, and a fine pass walks individual
// voxels inside occupied bricks. Hits write real depth, so voxels
// depth-composite with the rasterized scene. This is the Teardown-style
// storage model (CPU-built 3D texture + fragment DDA); contrast with
// VoxelChunkPass, which meshes 1m macro voxels into triangles.
//
// The DDA and the demo-volume generator are ports of the (reference-
// validated) implementation in project-vapor's Metal renderer.

// ---- Helper: draw the shared full-screen quad --------------------------------
static void DrawScreenQuadVAO(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}


// ---- OpenGL upload ------------------------------------------------------------

void MicroVoxelPass::RegisterVolume(VoxelVolumeComponent* v) {
    if (!v) return;
    for (auto* existing : _volumes)
        if (existing == v) return;
    _volumes.push_back(v);
}

void MicroVoxelPass::UnregisterVolume(VoxelVolumeComponent* v) {
    for (size_t i = 0; i < _volumes.size(); i++) {
        if (_volumes[i] == v) {
            _volumes.erase(_volumes.begin() + static_cast<long>(i));
            break;
        }
    }
    auto it = _glVolumes.find(v);
    if (it != _glVolumes.end()) {
        glDeleteTextures(1, &it->second.volumeTex);
        glDeleteTextures(1, &it->second.occupancyTex);
        glDeleteTextures(1, &it->second.paletteTex);
        _glVolumes.erase(it);
    }
    if (_uploadedVolume == v) _uploadedVolume = nullptr;// force re-upload of whatever renders next
}

void MicroVoxelPass::_uploadGL(VoxelVolumeComponent* v) {
    GLVolume& gv = _glVolumes[v];// creates the entry on first upload
    const auto N = static_cast<GLsizei>(v->gridDim);
    const auto BG = static_cast<GLsizei>(v->gridDim / v->brickDim);

    if (gv.volumeTex == 0) {
        glGenTextures(1, &gv.volumeTex);
        glGenTextures(1, &gv.occupancyTex);
        glGenTextures(1, &gv.paletteTex);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    auto setupIntTexParams = [](GLenum target) {
        // Integer textures require NEAREST filtering
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    };

    glBindTexture(GL_TEXTURE_3D, gv.volumeTex);
    setupIntTexParams(GL_TEXTURE_3D);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8UI, N, N, N, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, v->volume.data());

    glBindTexture(GL_TEXTURE_3D, gv.occupancyTex);
    setupIntTexParams(GL_TEXTURE_3D);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8UI, BG, BG, BG, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, v->occupancy.data());

    glBindTexture(GL_TEXTURE_2D, gv.paletteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 256x2: row 0 albedo+emission, row 1 material params (reflectivity/roughness)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, v->paletteRGBA.data());

    glBindTexture(GL_TEXTURE_3D, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Volume changed: accumulated GI history no longer matches the geometry.
    for (int i = 0; i < 2; i++) {
        if (_giFBOGL[i] != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[i]);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Creates/resizes the GI accumulation ping-pong targets, the shared normal
// target (MRT attachment 1), and the à-trous denoiser ping-pong; GI history is
// cleared on (re)creation (alpha 0 = invalid sample).
void MicroVoxelPass::_ensureGIRenderTargets(int w, int h) {
    if (_giW == w && _giH == h && _giTexGL[0] != 0) return;
    _giW = w;
    _giH = h;

    auto makeRGBA16F = [w, h](GLuint& tex) {
        if (tex == 0) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    };

    // Shared normal target — attached as MRT slot 1 on both GI FBOs (only the
    // one being traced writes it each frame; the à-trous reads it).
    makeRGBA16F(_giNormalTexGL);

    const GLenum giBufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    for (int i = 0; i < 2; i++) {
        if (_giFBOGL[i] == 0) glGenFramebuffers(1, &_giFBOGL[i]);
        makeRGBA16F(_giTexGL[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _giTexGL[i], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _giNormalTexGL, 0);
        glDrawBuffers(2, giBufs);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // À-trous ping-pong (single attachment each).
    const GLenum oneBuf[1] = { GL_COLOR_ATTACHMENT0 };
    for (int i = 0; i < 2; i++) {
        if (_atrousFBOGL[i] == 0) glGenFramebuffers(1, &_atrousFBOGL[i]);
        makeRGBA16F(_atrousTexGL[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, _atrousFBOGL[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _atrousTexGL[i], 0);
        glDrawBuffers(1, oneBuf);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---- WebGPU init / upload ------------------------------------------------------

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// Layout must match Uniforms in MICROVOXEL_WGSL:
//   2 mat4 + 4 vec4f + 2 vec4i + 1 vec4f + 2 x array<vec4f,4> = 240 + 128.
static constexpr uint64_t MICROVOXEL_UNIFORM_SIZE = 368;

MicroVoxelPass::~MicroVoxelPass() {
    if (_texBG) wgpuBindGroupRelease(_texBG);
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_texBGL) wgpuBindGroupLayoutRelease(_texBGL);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_pipeline) wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_volumeTexGPU) wgpuTextureRelease(_volumeTexGPU);
    if (_occupancyTexGPU) wgpuTextureRelease(_occupancyTexGPU);
    if (_paletteTexGPU) wgpuTextureRelease(_paletteTexGPU);
}

void MicroVoxelPass::_initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;
    {
        WGPUBufferDescriptor d{};
        d.size = MICROVOXEL_UNIFORM_SIZE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    auto p = GpuPipelineBuilder(device)
                 .wgsl(MICROVOXEL_WGSL)
                 .bgl({ gpuUniform(0, wgsl_stage::both, MICROVOXEL_UNIFORM_SIZE) })
                 .bgl({ gpuUintTexture3D(0), gpuUintTexture3D(1), gpuTexture(2) })
                 .colorFormat(colorFormat)
                 .depth(true, WGPUCompareFunction_Less)
                 .multisample(sampleCount)
                 .build();
    _pipeline = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL = p.bgl(1);
    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, MICROVOXEL_UNIFORM_SIZE).build();
}

void MicroVoxelPass::_uploadGPU(VoxelVolumeComponent* v) {
    const auto N = static_cast<uint32_t>(v->gridDim);
    const auto BG = static_cast<uint32_t>(v->gridDim / v->brickDim);

    // Recreate on every upload: sizes can change with gridDim, and WebGPU
    // textures are immutable in size/format.
    if (_texBG) {
        wgpuBindGroupRelease(_texBG);
        _texBG = nullptr;
    }
    if (_volumeTexGPU) {
        wgpuTextureRelease(_volumeTexGPU);
        _volumeTexGPU = nullptr;
    }
    if (_occupancyTexGPU) {
        wgpuTextureRelease(_occupancyTexGPU);
        _occupancyTexGPU = nullptr;
    }
    if (_paletteTexGPU) {
        wgpuTextureRelease(_paletteTexGPU);
        _paletteTexGPU = nullptr;
    }

    auto make3D = [&](uint32_t dim, const uint8_t* data) -> WGPUTexture {
        WGPUTextureDescriptor td{};
        td.size = { dim, dim, dim };
        td.format = WGPUTextureFormat_R8Uint;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_3D;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(_gpuDevice, &td);

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = dim;// 1 byte per texel; writeTexture has no 256B row alignment requirement
        layout.rowsPerImage = dim;
        WGPUExtent3D extent{ dim, dim, dim };
        wgpuQueueWriteTexture(_gpuQueue, &dst, data, static_cast<size_t>(dim) * dim * dim, &layout, &extent);
        return tex;
    };

    _volumeTexGPU = make3D(N, v->volume.data());
    _occupancyTexGPU = make3D(BG, v->occupancy.data());

    {
        WGPUTextureDescriptor td{};
        td.size = { 256, 2, 1 };// row 0 albedo+emission, row 1 material params
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        _paletteTexGPU = wgpuDeviceCreateTexture(_gpuDevice, &td);

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _paletteTexGPU;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 256 * 4;
        layout.rowsPerImage = 2;
        WGPUExtent3D extent{ 256, 2, 1 };
        wgpuQueueWriteTexture(_gpuQueue, &dst, v->paletteRGBA.data(), v->paletteRGBA.size(), &layout, &extent);
    }

    _texBG = GpuBindGroupBuilder(_gpuDevice, _texBGL)
                 .texture(0, _volumeTexGPU)
                 .texture(1, _occupancyTexGPU)
                 .texture(2, _paletteTexGPU)
                 .build();
}

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

// ---- Execute --------------------------------------------------------------------

void MicroVoxelPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    if (_volumes.empty()) return;
    VoxelVolumeComponent* vol = _volumes[0];// stage 1: render the first registered volume
    if (vol->volume.empty()) return;

    // Read grid config + world origin from the active volume; the rest of the
    // pass uses these as locals so the uniform setup below is unchanged.
    const int gridDim = vol->gridDim;
    const int brickDim = vol->brickDim;
    const float voxelSize = vol->voxelSize;
    const glm::vec3 volumeOrigin = vol->GetOrigin();

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent* light = ctx->GetMainLight();

    const glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const glm::vec3 cameraPos = camera->GetEyePosition();
    const glm::vec3 sunDir = light ? glm::normalize(-light->direction) : glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
    const glm::vec3 sunColor = light ? light->diffuse : glm::vec3(1.0f, 0.96f, 0.9f);
    // Respect the main light's intensity; sunIntensity is just an artistic gain.
    const float sunIntensityEff = sunIntensity * (light ? light->intensity : 1.0f);
    // Ambient level also follows the light. MicroVoxel keeps its sky-hemisphere
    // gradient (nicer than a flat fill), so the pass 'ambient' scalar is a gain
    // scaled by the light's ambient magnitude (its average channel).
    const float ambientEff = ambient * (light ? (light->ambient.r + light->ambient.g + light->ambient.b) / 3.0f : 1.0f);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!renderer.sceneRT) return;
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, static_cast<uint32_t>(renderer.sceneRT->GetNumSamples()));
        }
        // WebGPU stays single-volume (renders _volumes[0]) for now.
        if (vol->dirty || _uploadedVolume != vol) {
            _uploadGPU(vol);
            vol->dirty = false;
            _uploadedVolume = vol;
        }
        if (!_texBG) return;

        const int lightCount = std::min(pointLightCount, kMaxPointLights);
        struct {
            glm::mat4 invViewProj;
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 originVoxel;
            glm::vec4 sunDirInt;
            glm::vec4 sunColAmb;
            glm::ivec4 gridDim;
            glm::ivec4 misc;
            glm::vec4 params;
            glm::vec4 pointPosRadius[kMaxPointLights];
            glm::vec4 pointColor[kMaxPointLights];
        } u{
            invViewProj,
            viewProj,
            glm::vec4(cameraPos, 1.0f),
            glm::vec4(volumeOrigin, voxelSize),
            glm::vec4(sunDir, sunIntensityEff),
            glm::vec4(sunColor, ambientEff),
            glm::ivec4(gridDim, gridDim, gridDim, brickDim),
            glm::ivec4(maxRaySteps, shadowEnabled ? 1 : 0, reflectionsEnabled ? 1 : 0, lightCount),
            glm::vec4(aoStrength, emissiveStrength, 0.0f, 0.0f),
            {},
            {},
        };
        for (int i = 0; i < lightCount; i++) {
            u.pointPosRadius[i] = glm::vec4(pointLightPos[i], pointLightRadius[i]);
            u.pointColor[i] = glm::vec4(pointLightColor[i] * pointLightIntensity[i], 0.0f);
        }
        static_assert(sizeof(u) == MICROVOXEL_UNIFORM_SIZE, "uniform layout must match MICROVOXEL_WGSL");
        wgpuQueueWriteBuffer(_gpuQueue, _uniformBuf, 0, &u, sizeof(u));

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        auto* rt = static_cast<GPURenderTarget*>(renderer.sceneRT.get());
        rt->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _uniformBG, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 1, _texBG, 0, nullptr);
        wgpuRenderPassEncoderDraw(gpuEnc->pass, 3, 1, 0, 0);
        rt->End();
        return;
    }
#endif

    // ── OpenGL / WebGL2 path ─────────────────────────────────────────────────
    if (renderer.gl.screenQuadVAO == 0) return;

    ShaderProgram* shader = AssetManager::Get().GetShader("microvoxel");
    if (!shader) return;

    // Upload every registered volume's textures (lazy; re-upload on dirty).
    for (auto* v : _volumes) {
        if (v->volume.empty()) continue;
        if (v->dirty || _glVolumes.find(v) == _glVolumes.end()) {
            _uploadGL(v);
            v->dirty = false;
        }
    }
    const GLVolume& primaryTex = _glVolumes[vol];// vol == _volumes[0]; GI traces this one

    auto [width, height] = Window::Get()->GetPhysicalSize();

    // ── GI subpass: trace one diffuse bounce per pixel and accumulate ───────
    bool giActive = giStrength > 0.0f;
    ShaderProgram* giShader = giActive ? AssetManager::Get().GetShader("microvoxel_gi") : nullptr;
    // GI traces at reduced resolution (indirect light is low frequency); the
    // composite upsamples with bilinear filtering via uv sampling.
    const float giScale = glm::clamp(giResolutionScale, 0.25f, 1.0f);
    const int giW = std::max(1, static_cast<int>(static_cast<float>(width) * giScale));
    const int giH = std::max(1, static_cast<int>(static_cast<float>(height) * giScale));
    GLuint giResultTex = 0;// GI texture the composite samples (denoised when enabled)
    if (giActive && giShader) {
        _ensureGIRenderTargets(giW, giH);
        const int prev = 1 - _giCur;
        if (!_prevViewValid) {// first frame: reproject onto itself (blend rejects via alpha 0)
            _prevViewProj = viewProj;
            _prevCameraPos = cameraPos;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[_giCur]);
        glViewport(0, 0, giW, giH);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        // CRITICAL: the GI target's alpha channel carries camera distance for
        // history validation, not coverage. If an earlier pass (e.g. SunPass's
        // additive billboard) left GL_BLEND enabled, that alpha would be used
        // as a blend factor and the accumulation buffer diverges into noise.
        glDisable(GL_BLEND);

        giShader->Activate();
        giShader->SetUniform(std::string("u_invViewProj"), invViewProj);
        giShader->SetUniform(std::string("u_prevViewProj"), _prevViewProj);
        giShader->SetUniform(std::string("u_cameraPos"), cameraPos);
        giShader->SetUniform(std::string("u_prevCameraPos"), _prevCameraPos);
        giShader->SetUniform(std::string("u_volumeOrigin"), volumeOrigin);
        giShader->SetUniform(std::string("u_voxelSize"), voxelSize);
        giShader->SetUniform(std::string("u_gridDim"), gridDim);
        giShader->SetUniform(std::string("u_brickDim"), brickDim);
        giShader->SetUniform(std::string("u_maxRaySteps"), maxRaySteps);
        giShader->SetUniform(std::string("u_sunDir"), sunDir);
        giShader->SetUniform(std::string("u_sunColor"), sunColor);
        giShader->SetUniform(std::string("u_sunIntensity"), sunIntensityEff);
        giShader->SetUniform(std::string("u_frameIndex"), _giFrame);
        giShader->SetUniform(std::string("u_blend"), giBlend);
        giShader->SetUniform(std::string("u_emissiveStrength"), emissiveStrength);
        giShader->SetUniform(std::string("u_volume"), 0);
        giShader->SetUniform(std::string("u_occupancy"), 1);
        giShader->SetUniform(std::string("u_palette"), 2);
        giShader->SetUniform(std::string("u_history"), 3);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, primaryTex.volumeTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, primaryTex.occupancyTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, primaryTex.paletteTex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, _giTexGL[prev]);

        DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
        _giFrame++;

        // ── Spatial denoise: à-trous edge-stopping over the accumulated GI ──
        // History stays the raw temporal accumulation (_giTexGL); the à-trous
        // output is display-only, so filtered results are never fed back into
        // history (no progressive over-blurring). The viewport is still giW×giH
        // from the trace above. The composite samples giResultTex.
        giResultTex = _giTexGL[_giCur];
        ShaderProgram* atrous = giAtrousIterations > 0 ? AssetManager::Get().GetShader("microvoxel_atrous") : nullptr;
        if (atrous) {
            atrous->Activate();
            atrous->SetUniform(
                std::string("u_texelSize"), glm::vec2(1.0f / static_cast<float>(giW), 1.0f / static_cast<float>(giH))
            );
            atrous->SetUniform(std::string("u_sigmaDepth"), giSigmaDepth);
            atrous->SetUniform(std::string("u_sigmaNormal"), giSigmaNormal);
            atrous->SetUniform(std::string("u_sigmaLuma"), giSigmaLuma);
            atrous->SetUniform(std::string("u_gi"), 0);
            atrous->SetUniform(std::string("u_giNormal"), 1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, _giNormalTexGL);// constant across iterations
            GLuint src = _giTexGL[_giCur];
            int dst = 0;
            for (int it = 0; it < giAtrousIterations; it++) {
                glBindFramebuffer(GL_FRAMEBUFFER, _atrousFBOGL[dst]);
                atrous->SetUniform(std::string("u_stepSize"), 1 << it);// 1, 2, 4, ...
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src);
                DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
                src = _atrousTexGL[dst];
                dst = 1 - dst;
            }
            giResultTex = src;
        }
    } else {
        giActive = false;
    }

    // Roll the reprojection sources forward for the next frame
    _prevViewProj = viewProj;
    _prevCameraPos = cameraPos;
    _prevViewValid = true;

    glViewport(0, 0, width, height);

    // Render into the same target as ForwardOpaquePass (no clear; depth test
    // against the rasterized scene, and hits write depth for later passes)
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(renderer.sceneRT.get())->GetNativeFBOID());

    // ── Main pass: one bounding box per volume, depth-composited ─────────────
    // Frame-global uniforms are set once; per-volume grid/transform/textures are
    // set inside the loop. Each volume draws its world AABB (unit cube scaled by
    // u_model); the shader writes gl_FragDepth so overlapping volumes and the
    // rasterized scene composite correctly.
    shader->Activate();
    shader->SetUniform(std::string("u_viewProj"), viewProj);
    shader->SetUniform(std::string("u_viewportSize"), glm::vec2(width, height));
    shader->SetUniform(std::string("u_cameraPos"), cameraPos);
    shader->SetUniform(std::string("u_maxRaySteps"), maxRaySteps);
    shader->SetUniform(std::string("u_sunDir"), sunDir);
    shader->SetUniform(std::string("u_sunColor"), sunColor);
    shader->SetUniform(std::string("u_sunIntensity"), sunIntensityEff);
    shader->SetUniform(std::string("u_ambient"), ambientEff);
    shader->SetUniform(std::string("u_shadowEnabled"), shadowEnabled ? 1 : 0);
    shader->SetUniform(std::string("u_aoStrength"), aoStrength);
    shader->SetUniform(std::string("u_debugMode"), debugMode);
    shader->SetUniform(std::string("u_emissiveStrength"), emissiveStrength);
    shader->SetUniform(std::string("u_reflectionsEnabled"), reflectionsEnabled ? 1 : 0);
    const int lightCount = std::min(pointLightCount, kMaxPointLights);
    shader->SetUniform(std::string("u_pointLightCount"), lightCount);
    for (int i = 0; i < lightCount; i++) {
        const std::string idx = "[" + std::to_string(i) + "]";
        shader->SetUniform(std::string("u_pointLightPos") + idx, pointLightPos[i]);
        // Fold intensity into the color the shader receives, so the shader loop
        // stays a plain scaled-radiance accumulation.
        shader->SetUniform(std::string("u_pointLightColor") + idx, pointLightColor[i] * pointLightIntensity[i]);
        shader->SetUniform(std::string("u_pointLightRadius") + idx, pointLightRadius[i]);
    }
    shader->SetUniform(std::string("u_volume"), 0);
    shader->SetUniform(std::string("u_occupancy"), 1);
    shader->SetUniform(std::string("u_palette"), 2);
    shader->SetUniform(std::string("u_giTex"), 3);
    shader->SetUniform(std::string("u_giRaw"), 4);
    shader->SetUniform(std::string("u_giSplitX"), giActive ? giSplitCompare : -1.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    // Cull disabled so each box still covers its footprint when the camera is
    // inside it (front faces would be clipped); the shader writes gl_FragDepth
    // from the DDA hit, so the late depth test composites correctly regardless.
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);// voxel fragments are opaque (alpha=1); don't inherit a blend state

    for (auto* v : _volumes) {
        if (v->volume.empty()) continue;
        auto texIt = _glVolumes.find(v);
        if (texIt == _glVolumes.end()) continue;
        const GLVolume& gv = texIt->second;

        const int vGrid = v->gridDim;
        const float vVox = v->voxelSize;
        const glm::vec3 vOrigin = v->GetOrigin();
        const float boxExtent = static_cast<float>(vGrid) * vVox;
        const glm::vec3 boxCenter = vOrigin + glm::vec3(boxExtent * 0.5f);
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), boxCenter) * glm::scale(glm::mat4(1.0f), glm::vec3(boxExtent * 0.5f));

        // GI is traced against the primary volume only, so its screen-space
        // buffer is valid only for that volume's pixels; others use flat ambient.
        const bool useGI = giActive && v == vol;

        shader->SetUniform(std::string("u_model"), model);
        shader->SetUniform(std::string("u_volumeOrigin"), vOrigin);
        shader->SetUniform(std::string("u_voxelSize"), vVox);
        shader->SetUniform(std::string("u_gridDim"), vGrid);
        shader->SetUniform(std::string("u_brickDim"), v->brickDim);
        shader->SetUniform(std::string("u_giStrength"), useGI ? giStrength : 0.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, gv.volumeTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, gv.occupancyTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gv.paletteTex);
        // Always bind a valid 2D texture to unit 3: macOS GL validates every
        // declared sampler even when its branch is dead (giStrength == 0), and
        // warns + substitutes a zero texture otherwise. This volume's palette
        // stands in when GI is off (the composite doesn't sample u_giTex then).
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, (useGI && giResultTex) ? giResultTex : gv.paletteTex);
        // Unit 4: raw (un-denoised) GI for the split-screen compare. Fall back to
        // this volume's palette when GI is off (macOS validates every sampler).
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, (useGI && _giTexGL[_giCur]) ? _giTexGL[_giCur] : gv.paletteTex);

        glBindVertexArray(renderer.gl.skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);// unit cube -> volume AABB via u_model
        glBindVertexArray(0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);

    if (giActive) _giCur = 1 - _giCur;// ping-pong for the next frame
}

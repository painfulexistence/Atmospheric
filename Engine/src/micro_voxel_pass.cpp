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
// Renders dense voxel volumes (5 cm voxels by default) with a per-volume
// two-level DDA raymarch: each volume rasterizes its tight bounding box and
// every covered pixel marches the volume's 3D texture in the volume's LOCAL
// space (so physics-driven rotation works) — a coarse pass strides
// brick-by-brick, skipping empty bricks via the occupancy texture, and a fine
// pass walks individual voxels inside occupied bricks. Hits write real depth,
// so volumes depth-composite with each other and the rasterized scene. This is
// the Teardown-style storage model (CPU-built 3D texture + fragment DDA);
// contrast with VoxelChunkPass, which meshes 1m macro voxels into triangles.
//
// Scaling behaviors (the "many objects" contract):
//  - volumes are frustum-culled and drawn near-to-far with front-face culling
//    (exactly one marched fragment per covered pixel);
//  - runtime edits upload only the edited sub-box (a carve is KBs, not the
//    full grid) and preserve GI history;
//  - the GI pass traces only the nearest-K volumes; other volumes' pixels fall
//    back to flat ambient via the GI sample validity check in the composite.
//
// The DDA and the demo-volume generator are ports of the (reference-
// validated) implementation in project-vapor's Metal renderer.

// ---- Helper: draw the shared full-screen quad --------------------------------
static void DrawScreenQuadVAO(GLuint vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ---- Helpers: frustum culling ------------------------------------------------

// Gribb-Hartmann plane extraction from a viewProj matrix (GL clip conventions).
// Planes point inward; a point p is inside when dot(plane.xyz, p) + plane.w > 0.
static void MvExtractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) {
    auto row = [&vp](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    planes[0] = r3 + r0;// left
    planes[1] = r3 - r0;// right
    planes[2] = r3 + r1;// bottom
    planes[3] = r3 - r1;// top
    planes[4] = r3 + r2;// near
    planes[5] = r3 - r2;// far
}

// Conservative OBB-vs-frustum: the local-space bounds' 8 corners are taken to
// world space; a volume is culled only when all corners are outside one plane.
static bool MvBoxVisible(const glm::vec4 planes[6], const glm::mat4& model, const glm::vec3& bMinL, const glm::vec3& bMaxL) {
    glm::vec3 corners[8];
    for (int i = 0; i < 8; i++) {
        const glm::vec3 c(
            (i & 1) ? bMaxL.x : bMinL.x, (i & 2) ? bMaxL.y : bMinL.y, (i & 4) ? bMaxL.z : bMinL.z
        );
        corners[i] = glm::vec3(model * glm::vec4(c, 1.0f));
    }
    for (int p = 0; p < 6; p++) {
        bool anyInside = false;
        for (int i = 0; i < 8; i++) {
            if (glm::dot(glm::vec3(planes[p]), corners[i]) + planes[p].w > 0.0f) {
                anyInside = true;
                break;
            }
        }
        if (!anyInside) return false;
    }
    return true;
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

// Upload an inclusive voxel sub-box of a dense R8UI grid into an existing 3D
// texture. The source pointer is the full grid; GL's unpack skip/stride
// parameters address the sub-box in place (supported on GL 4.1, GLES 3, and
// WebGL2), so no staging copy is needed.
static void MvUploadSubRegion3D(GLuint tex, const uint8_t* fullGrid, int fullDim, glm::ivec3 lo, glm::ivec3 hi) {
    lo = glm::clamp(lo, glm::ivec3(0), glm::ivec3(fullDim - 1));
    hi = glm::clamp(hi, glm::ivec3(0), glm::ivec3(fullDim - 1));
    const glm::ivec3 size = hi - lo + 1;
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) return;

    glBindTexture(GL_TEXTURE_3D, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, fullDim);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, fullDim);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, lo.x);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, lo.y);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, lo.z);
    glTexSubImage3D(
        GL_TEXTURE_3D, 0, lo.x, lo.y, lo.z, size.x, size.y, size.z, GL_RED_INTEGER, GL_UNSIGNED_BYTE, fullGrid
    );
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
}

void MicroVoxelPass::_uploadGL(VoxelVolumeComponent* v) {
    GLVolume& gv = _glVolumes[v];// creates the entry on first upload
    const auto N = static_cast<GLsizei>(v->gridDim);
    const auto BG = static_cast<GLsizei>(v->gridDim / v->brickDim);

    const bool fresh = (gv.volumeTex == 0);
    if (fresh) {
        glGenTextures(1, &gv.volumeTex);
        glGenTextures(1, &gv.occupancyTex);
        glGenTextures(1, &gv.paletteTex);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (!fresh && !v->fullDirty) {
        // Runtime edit: upload only the recorded voxel sub-box (and the brick
        // range it spans in the occupancy grid). A carve touches ~KBs instead
        // of the full grid (16.8 MB at 256^3), and GI history stays valid —
        // the GI pass's per-pixel distance check rejects stale samples where
        // the surface actually moved.
        if (v->HasDirtyRegion()) {
            MvUploadSubRegion3D(gv.volumeTex, v->volume.data(), N, v->dirtyMin, v->dirtyMax);
            MvUploadSubRegion3D(
                gv.occupancyTex, v->occupancy.data(), BG, v->dirtyMin / v->brickDim, v->dirtyMax / v->brickDim
            );
        }
        return;
    }

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

    // Full re-generation: accumulated GI history no longer matches the
    // geometry at all, so reset it. (Partial edits above keep history.)
    for (int i = 0; i < 2; i++) {
        if (_giFBOGL[i] != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[i]);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The volume bounding box mesh: a unit cube with consistent outward CCW
// winding (the shared skybox cube is mixed-winding and can't be face-culled).
// The pass draws it with front-face culling, so each covered pixel gets exactly
// one fragment — from the box's far side — whether the camera is outside or
// inside the volume.
void MicroVoxelPass::_ensureCubeVAO() {
    if (_cubeVAO != 0) return;
    // clang-format off
    static const float kCube[] = {
        // -Z
        -1,-1,-1,  -1, 1,-1,   1, 1,-1,   -1,-1,-1,   1, 1,-1,   1,-1,-1,
        // +Z
        -1,-1, 1,   1,-1, 1,   1, 1, 1,   -1,-1, 1,   1, 1, 1,  -1, 1, 1,
        // -X
        -1,-1,-1,  -1,-1, 1,  -1, 1, 1,   -1,-1,-1,  -1, 1, 1,  -1, 1,-1,
        // +X
         1,-1,-1,   1, 1,-1,   1, 1, 1,    1,-1,-1,   1, 1, 1,   1,-1, 1,
        // -Y
        -1,-1,-1,   1,-1,-1,   1,-1, 1,   -1,-1,-1,   1,-1, 1,  -1,-1, 1,
        // +Y
        -1, 1,-1,  -1, 1, 1,   1, 1, 1,   -1, 1,-1,   1, 1, 1,   1, 1,-1,
    };
    // clang-format on
    glGenVertexArrays(1, &_cubeVAO);
    glGenBuffers(1, &_cubeVBO);
    glBindVertexArray(_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCube), kCube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
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

    // Partial path: the textures already hold this volume at this size, and
    // only a sub-box changed (a carve). writeTexture with a dst origin and the
    // full grid as a strided source uploads just that region.
    if (_volumeTexGPU && _uploadedVolume == v && _gpuVolDim == N && !v->fullDirty && v->HasDirtyRegion()) {
        auto writeSub = [this](WGPUTexture tex, const std::vector<uint8_t>& data, uint32_t dim, glm::ivec3 lo,
                               glm::ivec3 hi) {
            lo = glm::clamp(lo, glm::ivec3(0), glm::ivec3(static_cast<int>(dim) - 1));
            hi = glm::clamp(hi, glm::ivec3(0), glm::ivec3(static_cast<int>(dim) - 1));
            if (hi.x < lo.x || hi.y < lo.y || hi.z < lo.z) return;
            WGPUTexelCopyTextureInfo dst{};
            dst.texture = tex;
            dst.aspect = WGPUTextureAspect_All;
            dst.origin = { static_cast<uint32_t>(lo.x), static_cast<uint32_t>(lo.y), static_cast<uint32_t>(lo.z) };
            WGPUTexelCopyBufferLayout layout{};
            layout.offset = static_cast<uint64_t>(lo.z) * dim * dim + static_cast<uint64_t>(lo.y) * dim
                            + static_cast<uint64_t>(lo.x);
            layout.bytesPerRow = dim;// 1 byte per texel; source rows stride the full grid
            layout.rowsPerImage = dim;
            WGPUExtent3D extent{ static_cast<uint32_t>(hi.x - lo.x + 1), static_cast<uint32_t>(hi.y - lo.y + 1),
                                 static_cast<uint32_t>(hi.z - lo.z + 1) };
            wgpuQueueWriteTexture(_gpuQueue, &dst, data.data(), data.size(), &layout, &extent);
        };
        writeSub(_volumeTexGPU, v->volume, N, v->dirtyMin, v->dirtyMax);
        writeSub(_occupancyTexGPU, v->occupancy, BG, v->dirtyMin / v->brickDim, v->dirtyMax / v->brickDim);
        return;
    }

    // Recreate on full upload: sizes can change with gridDim, and WebGPU
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
    _gpuVolDim = N;

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
    // Representative volume: palette source and the WebGPU single-volume
    // target. First registered non-empty volume (the demo registers the big
    // terrain first).
    VoxelVolumeComponent* vol = nullptr;
    for (auto* v : _volumes) {
        if (!v->volume.empty()) {
            vol = v;
            break;
        }
    }
    if (!vol) return;

    // Representative grid config; the GI subpass uses these as fallbacks. The
    // GL main loop reads per-volume values in its own loop.
    const int brickDim = vol->brickDim;
    const float voxelSize = vol->voxelSize;

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
        // WebGPU stays single-volume (renders the representative volume) and
        // translation-only for now.
        const int gridDim = vol->gridDim;
        const glm::vec3 volumeOrigin = vol->GetOrigin();
        if (vol->dirty || _uploadedVolume != vol) {
            _uploadGPU(vol);
            vol->ClearDirty();
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

    // Upload every registered volume's textures (lazy; sub-region on edits,
    // full on (re)generation).
    for (auto* v : _volumes) {
        if (v->volume.empty()) continue;
        if (v->dirty || _glVolumes.find(v) == _glVolumes.end()) {
            _uploadGL(v);
            v->ClearDirty();
        }
    }

    // Per-frame volume data: transforms, tight bounds, camera distance, and
    // whether the transform changed since last frame (GI history rejection).
    struct VolDraw {
        VoxelVolumeComponent* v;
        const GLVolume* gl;
        glm::mat4 model;
        glm::mat4 invModel;
        glm::vec3 bMinL, bMaxL;// tight local-space bounds
        float dist;
        bool moved;
    };
    std::vector<VolDraw> draws;
    draws.reserve(_volumes.size());
    for (auto* v : _volumes) {
        if (v->volume.empty() || !v->HasSolid()) continue;
        auto it = _glVolumes.find(v);
        if (it == _glVolumes.end()) continue;
        VolDraw d;
        d.v = v;
        d.gl = &it->second;
        d.model = v->GetModelMatrix();
        d.invModel = glm::inverse(d.model);
        d.bMinL = v->GetLocalBoundsMin();
        d.bMaxL = v->GetLocalBoundsMax();
        const glm::vec3 centerW = glm::vec3(d.model * glm::vec4(0.5f * (d.bMinL + d.bMaxL), 1.0f));
        // Approximate distance to the volume's SURFACE (center minus half
        // diagonal), so a big terrain whose center is far doesn't lose to small
        // nearby props in the nearest-K sort.
        d.dist = glm::max(0.0f, glm::length(centerW - cameraPos) - 0.5f * glm::length(d.bMaxL - d.bMinL));
        d.moved = it->second.prevModelValid && (d.model != it->second.prevModel);
        draws.push_back(d);
    }

    auto rollPrevModels = [this, &draws]() {
        for (const auto& d : draws) {
            GLVolume& gv = _glVolumes[d.v];
            gv.prevModel = d.model;
            gv.prevModelValid = true;
        }
    };

    if (draws.empty()) {
        rollPrevModels();
        return;
    }

    const GLVolume& primaryTex = *(_glVolumes.find(vol) != _glVolumes.end() ? &_glVolumes[vol] : draws[0].gl);

    // Nearest-K GI set: sorted by camera distance so the traced volumes are the
    // ones dominating the view. No frustum culling here — off-screen volumes
    // still bounce light into the frame.
    std::vector<const VolDraw*> sortedByDist;
    sortedByDist.reserve(draws.size());
    for (const auto& d : draws)
        sortedByDist.push_back(&d);
    std::sort(sortedByDist.begin(), sortedByDist.end(), [](const VolDraw* a, const VolDraw* b) {
        return a->dist < b->dist;
    });

    constexpr int kMaxGiVolumes = 4;// must match MAX_VOLUMES in microvoxel_gi.frag
    std::vector<const VolDraw*> giSet;
    if (giCrossVolume) {
        for (const VolDraw* d : sortedByDist) {
            giSet.push_back(d);
            if (static_cast<int>(giSet.size()) >= kMaxGiVolumes) break;
        }
    } else {
        // Primary volume only (old single-volume GI behavior).
        const VolDraw* primary = nullptr;
        for (const auto& d : draws)
            if (d.v == vol) primary = &d;
        giSet.push_back(primary ? primary : sortedByDist[0]);
    }
    auto inGiSet = [&giSet](const VoxelVolumeComponent* v) {
        for (const VolDraw* d : giSet)
            if (d->v == v) return true;
        return false;
    };

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
        giShader->SetUniform(std::string("u_voxelSize"), voxelSize);// representative, for ray offsets
        giShader->SetUniform(std::string("u_brickDim"), brickDim);
        giShader->SetUniform(std::string("u_maxRaySteps"), maxRaySteps);
        giShader->SetUniform(std::string("u_sunDir"), sunDir);
        giShader->SetUniform(std::string("u_sunColor"), sunColor);
        giShader->SetUniform(std::string("u_sunIntensity"), sunIntensityEff);
        giShader->SetUniform(std::string("u_frameIndex"), _giFrame);
        giShader->SetUniform(std::string("u_blend"), giBlend);
        giShader->SetUniform(std::string("u_emissiveStrength"), emissiveStrength);

        // Bind up to MAX_VOLUMES (matches the shader) volume/occupancy pairs on
        // units 0..7; every slot gets a valid texture (unused ones fall back to
        // the primary) for macOS sampler validation. u_volumeCount bounds the
        // shader's dispatch to the nearest-K set selected above.
        const int giCount = static_cast<int>(giSet.size());
        giShader->SetUniform(std::string("u_volumeCount"), giCount);
        const char* volNames[kMaxGiVolumes] = { "u_vol0", "u_vol1", "u_vol2", "u_vol3" };
        const char* occNames[kMaxGiVolumes] = { "u_occ0", "u_occ1", "u_occ2", "u_occ3" };
        for (int i = 0; i < kMaxGiVolumes; i++) {
            const VolDraw* d = (i < giCount) ? giSet[i] : giSet[0];
            const std::string idx = "[" + std::to_string(i) + "]";
            giShader->SetUniform(std::string("u_origins") + idx, d->v->GetLocalOrigin());
            giShader->SetUniform(std::string("u_boundsMin") + idx, d->bMinL);
            giShader->SetUniform(std::string("u_boundsMax") + idx, d->bMaxL);
            giShader->SetUniform(std::string("u_models") + idx, d->model);
            giShader->SetUniform(std::string("u_invModels") + idx, d->invModel);
            giShader->SetUniform(std::string("u_moved") + idx, d->moved ? 1 : 0);
            giShader->SetUniform(std::string("u_voxelSizes") + idx, d->v->voxelSize);
            giShader->SetUniform(std::string("u_gridDims") + idx, d->v->gridDim);
            giShader->SetUniform(std::string(volNames[i]), i * 2);
            giShader->SetUniform(std::string(occNames[i]), i * 2 + 1);
            glActiveTexture(GL_TEXTURE0 + i * 2);
            glBindTexture(GL_TEXTURE_3D, d->gl->volumeTex);
            glActiveTexture(GL_TEXTURE0 + i * 2 + 1);
            glBindTexture(GL_TEXTURE_3D, d->gl->occupancyTex);
        }
        giShader->SetUniform(std::string("u_palette"), 8);
        giShader->SetUniform(std::string("u_history"), 9);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, primaryTex.paletteTex);// palette shared across volumes
        glActiveTexture(GL_TEXTURE9);
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

    // ── Main pass: one tight bounding box per volume, depth-composited ───────
    // Frame-global uniforms are set once; per-volume grid/transform/textures are
    // set inside the loop. Volumes are frustum-culled and drawn near-to-far.
    // Each volume draws its tight OBB (unit cube scaled by u_model = objModel *
    // bounds); the shader writes gl_FragDepth so overlapping volumes and the
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
    // Front-face culling on a consistently-wound cube: exactly one fragment
    // (the box's far side) per covered pixel, from outside AND from inside the
    // volume — half the marched fragments of the old cull-disabled double-face
    // draw. The shader writes gl_FragDepth from the DDA hit, so the late depth
    // test composites correctly regardless.
    _ensureCubeVAO();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDisable(GL_BLEND);// voxel fragments are opaque (alpha=1); don't inherit a blend state

    // Frustum-cull, then draw near-to-far: with late-Z the ordering doesn't
    // save shading, but nearer volumes land their depth first so farther
    // fragments' writes are rejected instead of thrashing the depth buffer.
    glm::vec4 frustum[6];
    MvExtractFrustumPlanes(viewProj, frustum);
    std::vector<const VolDraw*> visible;
    visible.reserve(sortedByDist.size());
    for (const VolDraw* d : sortedByDist) {
        if (MvBoxVisible(frustum, d->model, d->bMinL, d->bMaxL)) visible.push_back(d);
    }

    for (const VolDraw* d : visible) {
        VoxelVolumeComponent* v = d->v;
        const GLVolume& gv = *d->gl;

        // Tight box in local space -> world via the object's rigid transform.
        const glm::vec3 centerL = 0.5f * (d->bMinL + d->bMaxL);
        const glm::vec3 halfL = 0.5f * (d->bMaxL - d->bMinL);
        const glm::mat4 boxModel =
            d->model * glm::translate(glm::mat4(1.0f), centerL) * glm::scale(glm::mat4(1.0f), halfL);

        // GI is valid only for pixels of volumes the GI pass traced; everything
        // else falls back to flat ambient (the shader also checks per-sample
        // validity, so overlaps degrade gracefully).
        const bool useGI = giActive && inGiSet(v);

        shader->SetUniform(std::string("u_model"), boxModel);
        shader->SetUniform(std::string("u_objModel"), d->model);
        shader->SetUniform(std::string("u_invObjModel"), d->invModel);
        shader->SetUniform(std::string("u_volumeOrigin"), v->GetLocalOrigin());
        shader->SetUniform(std::string("u_boundsMin"), d->bMinL);
        shader->SetUniform(std::string("u_boundsMax"), d->bMaxL);
        shader->SetUniform(std::string("u_voxelSize"), v->voxelSize);
        shader->SetUniform(std::string("u_gridDim"), v->gridDim);
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

        glBindVertexArray(_cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);// unit cube -> tight volume OBB via u_model
        glBindVertexArray(0);
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);

    rollPrevModels();
    if (giActive) _giCur = 1 - _giCur;// ping-pong for the next frame
}

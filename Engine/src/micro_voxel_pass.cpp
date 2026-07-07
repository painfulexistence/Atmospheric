#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "console_subsystem.hpp"
#include "gfx_factory.hpp"
#include "gl_render_target.hpp"
#include "graphics_subsystem.hpp"
#include "light_component.hpp"
#include "renderer.hpp"
#include "window.hpp"

#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <thread>

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

// ---- Procedural demo volume: deterministic hash noise ------------------------

static float MvHashNoise(int x, int y, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
               + static_cast<uint32_t>(z) * 2246822519u + seed * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

static float MvValueNoise3(glm::vec3 p, uint32_t seed) {
    glm::vec3 pf = glm::floor(p);
    glm::vec3 f = p - pf;
    f = f * f * (3.0f - 2.0f * f);// smoothstep
    int xi = static_cast<int>(pf.x), yi = static_cast<int>(pf.y), zi = static_cast<int>(pf.z);

    auto corner = [&](int dx, int dy, int dz) { return MvHashNoise(xi + dx, yi + dy, zi + dz, seed); };
    float c000 = corner(0, 0, 0), c100 = corner(1, 0, 0), c010 = corner(0, 1, 0), c110 = corner(1, 1, 0);
    float c001 = corner(0, 0, 1), c101 = corner(1, 0, 1), c011 = corner(0, 1, 1), c111 = corner(1, 1, 1);

    float x00 = glm::mix(c000, c100, f.x), x10 = glm::mix(c010, c110, f.x);
    float x01 = glm::mix(c001, c101, f.x), x11 = glm::mix(c011, c111, f.x);
    return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
}

static float MvFbm3(glm::vec3 p, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * MvValueNoise3(p, seed + static_cast<uint32_t>(i) * 101u);
        p *= 2.0f;
        amp *= 0.5f;
    }
    return sum;// ~[0, 1)
}

// Gradient (Perlin-style) noise for the terrain heightfield — value noise has
// axis-aligned plateaus that read as fake terrain; hashed unit gradients with
// a quintic fade give the classic smooth rolling look.
static float MvGradDot(int xi, int yi, int zi, glm::vec3 offset, uint32_t seed) {
    glm::vec3 g(
        MvHashNoise(xi, yi, zi, seed) - 0.5f,
        MvHashNoise(xi, yi, zi, seed ^ 0x9E3779B9u) - 0.5f,
        MvHashNoise(xi, yi, zi, seed ^ 0x85EBCA6Bu) - 0.5f
    );
    float len = glm::length(g);
    if (len < 1e-6f) return offset.x;
    return glm::dot(g / len, offset);
}

static float MvGradNoise3(glm::vec3 p, uint32_t seed) {
    glm::vec3 pf = glm::floor(p);
    glm::vec3 f = p - pf;
    glm::vec3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);// quintic fade
    int xi = static_cast<int>(pf.x), yi = static_cast<int>(pf.y), zi = static_cast<int>(pf.z);

    auto corner = [&](int dx, int dy, int dz) {
        return MvGradDot(xi + dx, yi + dy, zi + dz, f - glm::vec3(dx, dy, dz), seed);
    };
    float x00 = glm::mix(corner(0, 0, 0), corner(1, 0, 0), u.x);
    float x10 = glm::mix(corner(0, 1, 0), corner(1, 1, 0), u.x);
    float x01 = glm::mix(corner(0, 0, 1), corner(1, 0, 1), u.x);
    float x11 = glm::mix(corner(0, 1, 1), corner(1, 1, 1), u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);// ~[-0.7, 0.7]
}

static float MvGradFbm01(glm::vec3 p, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * MvGradNoise3(p, seed + static_cast<uint32_t>(i) * 101u);
        p *= 2.0f;
        amp *= 0.5f;
    }
    return glm::clamp(0.5f + sum * 1.2f, 0.0f, 1.0f);
}

// Fills the volume with a procedural terrain (heightfield + caves + ore
// speckles + floating crystal spheres), rebuilds the brick occupancy grid,
// marks everything for GPU upload and enables the pass.
void MicroVoxelPass::GenerateDemoVolume(uint32_t seed) {
    gridDim -= gridDim % brickDim;// Guard against non-brick-aligned sizes
    const uint32_t N = static_cast<uint32_t>(gridDim);
    const uint32_t B = static_cast<uint32_t>(brickDim);
    const uint32_t BG = N / B;

    _volume.assign(static_cast<size_t>(N) * N * N, 0);
    _occupancy.assign(static_cast<size_t>(BG) * BG * BG, 0);
    auto voxelAt = [this, N](uint32_t x, uint32_t y, uint32_t z) -> uint8_t& {
        return _volume[(static_cast<size_t>(z) * N + y) * N + x];
    };

    // Material palette (index 0 = air)
    enum : uint8_t { MatGrass = 1, MatDirt = 2, MatStone = 3, MatSnow = 4, MatSand = 5, MatOre = 6, MatCrystal = 7 };
    _paletteRGBA.assign(256 * 4, 0);
    auto setPalette = [this](uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
        _paletteRGBA[idx * 4 + 0] = r;
        _paletteRGBA[idx * 4 + 1] = g;
        _paletteRGBA[idx * 4 + 2] = b;
        _paletteRGBA[idx * 4 + 3] = 255;
    };
    for (int i = 8; i < 256; i++) setPalette(static_cast<uint8_t>(i), 128, 128, 128);
    setPalette(MatGrass, 64, 140, 46);
    setPalette(MatDirt, 107, 77, 46);
    setPalette(MatStone, 122, 122, 128);
    setPalette(MatSnow, 235, 240, 250);
    setPalette(MatSand, 204, 184, 122);
    setPalette(MatOre, 242, 191, 64);
    setPalette(MatCrystal, 115, 191, 242);

    // Terrain heightfield: gradient-noise fbm with gentle amplitude (~0.28
    // height/width ratio) so the terrain reads as rolling hills rather than
    // the 1:1-aspect spikes the first value-noise recipe produced.
    const float baseH = 0.10f * static_cast<float>(N);
    const float varH = 0.34f * static_cast<float>(N);
    const float snowLine = baseH + 0.75f * varH;
    const float sandLine = baseH + 0.12f * varH;
    std::vector<float> heights(static_cast<size_t>(N) * N);
    for (uint32_t z = 0; z < N; z++) {
        for (uint32_t x = 0; x < N; x++) {
            float h01 = MvGradFbm01(glm::vec3(x, 0.0f, z) * 0.010f, 5, seed);
            h01 = std::pow(h01, 1.6f);// Bias toward valleys with occasional peaks
            heights[z * N + x] = baseH + h01 * varH;
        }
    }

    // Column fill, parallelized over z slices (cells are disjoint per thread)
    const uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
    std::vector<uint32_t> threadSolidCounts(threadCount, 0);
    {
        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (uint32_t ti = 0; ti < threadCount; ti++) {
            workers.emplace_back([&, ti]() {
                uint32_t localSolid = 0;
                for (uint32_t z = ti; z < N; z += threadCount) {
                    for (uint32_t x = 0; x < N; x++) {
                        const float h = heights[z * N + x];
                        const auto top = static_cast<uint32_t>(h);
                        for (uint32_t y = 0; y <= top && y < N; y++) {
                            // Carve caves below the surface crust
                            if (y + 4 < top) {
                                float cave = MvFbm3(glm::vec3(x, y, z) * 0.045f, 3, seed + 7u);
                                if (cave > 0.62f) continue;
                            }
                            uint8_t mat = MatStone;
                            const uint32_t depth = top - y;
                            if (depth == 0) {
                                mat = (h > snowLine) ? MatSnow : ((h < sandLine) ? MatSand : MatGrass);
                            } else if (depth <= 3) {
                                mat = (h < sandLine) ? MatSand : MatDirt;
                            } else if (MvHashNoise(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), seed + 13u) > 0.995f) {
                                mat = MatOre;
                            }
                            voxelAt(x, y, z) = mat;
                            localSolid++;
                        }
                    }
                }
                threadSolidCounts[ti] = localSolid;
            });
        }
        for (auto& w : workers) w.join();
    }
    solidCount = 0;
    for (uint32_t c : threadSolidCounts) solidCount += c;

    // A few floating crystal spheres to show the volume is truly 3D
    for (int s = 0; s < 4; s++) {
        glm::vec3 center(
            (0.15f + 0.7f * MvHashNoise(s, 1, 0, seed + 23u)) * N,
            (0.70f + 0.22f * MvHashNoise(s, 2, 0, seed + 23u)) * N,
            (0.15f + 0.7f * MvHashNoise(s, 3, 0, seed + 23u)) * N
        );
        float radius = (0.02f + 0.03f * MvHashNoise(s, 4, 0, seed + 23u)) * N;
        int r = static_cast<int>(radius) + 1;
        for (int dz = -r; dz <= r; dz++) {
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (glm::length(glm::vec3(dx, dy, dz)) > radius) continue;
                    int x = static_cast<int>(center.x) + dx;
                    int y = static_cast<int>(center.y) + dy;
                    int z = static_cast<int>(center.z) + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(N) || y >= static_cast<int>(N)
                        || z >= static_cast<int>(N))
                        continue;
                    uint8_t& v = voxelAt(x, y, z);
                    if (v == 0) solidCount++;
                    v = MatCrystal;
                }
            }
        }
    }

    // Brick occupancy grid (any-solid per brick; empty bricks are skipped in
    // one coarse DDA step)
    for (uint32_t bz = 0; bz < BG; bz++) {
        for (uint32_t by = 0; by < BG; by++) {
            for (uint32_t bx = 0; bx < BG; bx++) {
                bool occupied = false;
                for (uint32_t z = bz * B; z < (bz + 1) * B && !occupied; z++) {
                    for (uint32_t y = by * B; y < (by + 1) * B && !occupied; y++) {
                        for (uint32_t x = bx * B; x < (bx + 1) * B; x++) {
                            if (voxelAt(x, y, z) != 0) {
                                occupied = true;
                                break;
                            }
                        }
                    }
                }
                _occupancy[(static_cast<size_t>(bz) * BG + by) * BG + bx] = occupied ? 1 : 0;
            }
        }
    }

    _dirty = true;
    enabled = true;

    if (auto* console = ConsoleSubsystem::Get()) {
        console->Info(fmt::format(
            "MicroVoxel demo volume generated: {}^3 grid, {} solid voxels ({:.1f}% fill)",
            N,
            solidCount,
            100.0f * static_cast<float>(solidCount) / static_cast<float>(static_cast<size_t>(N) * N * N)
        ));
    }
}

// ---- OpenGL upload ------------------------------------------------------------

void MicroVoxelPass::_uploadGL() {
    const auto N = static_cast<GLsizei>(gridDim);
    const auto BG = static_cast<GLsizei>(gridDim / brickDim);

    if (_volumeTexGL == 0) {
        glGenTextures(1, &_volumeTexGL);
        glGenTextures(1, &_occupancyTexGL);
        glGenTextures(1, &_paletteTexGL);
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

    glBindTexture(GL_TEXTURE_3D, _volumeTexGL);
    setupIntTexParams(GL_TEXTURE_3D);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8UI, N, N, N, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, _volume.data());

    glBindTexture(GL_TEXTURE_3D, _occupancyTexGL);
    setupIntTexParams(GL_TEXTURE_3D);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8UI, BG, BG, BG, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, _occupancy.data());

    glBindTexture(GL_TEXTURE_2D, _paletteTexGL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, _paletteRGBA.data());

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

    _dirty = false;
}

// Creates/resizes the GI accumulation ping-pong targets; history is cleared on
// (re)creation (alpha 0 = invalid sample).
void MicroVoxelPass::_ensureGIRenderTargets(int w, int h) {
    if (_giW == w && _giH == h && _giTexGL[0] != 0) return;
    _giW = w;
    _giH = h;
    for (int i = 0; i < 2; i++) {
        if (_giTexGL[i] == 0) {
            glGenTextures(1, &_giTexGL[i]);
            glGenFramebuffers(1, &_giFBOGL[i]);
        }
        glBindTexture(GL_TEXTURE_2D, _giTexGL[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _giTexGL[i], 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---- WebGPU init / upload ------------------------------------------------------

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// Layout must match Uniforms in MICROVOXEL_WGSL: 2 mat4 + 4 vec4f + 2 vec4i + 1 vec4f.
static constexpr uint64_t MICROVOXEL_UNIFORM_SIZE = 240;

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

void MicroVoxelPass::_uploadGPU() {
    const auto N = static_cast<uint32_t>(gridDim);
    const auto BG = static_cast<uint32_t>(gridDim / brickDim);

    // Recreate on every upload: sizes can change with gridDim, and WebGPU
    // textures are immutable in size/format.
    if (_texBG) { wgpuBindGroupRelease(_texBG); _texBG = nullptr; }
    if (_volumeTexGPU) { wgpuTextureRelease(_volumeTexGPU); _volumeTexGPU = nullptr; }
    if (_occupancyTexGPU) { wgpuTextureRelease(_occupancyTexGPU); _occupancyTexGPU = nullptr; }
    if (_paletteTexGPU) { wgpuTextureRelease(_paletteTexGPU); _paletteTexGPU = nullptr; }

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

    _volumeTexGPU = make3D(N, _volume.data());
    _occupancyTexGPU = make3D(BG, _occupancy.data());

    {
        WGPUTextureDescriptor td{};
        td.size = { 256, 1, 1 };
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
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 256, 1, 1 };
        wgpuQueueWriteTexture(_gpuQueue, &dst, _paletteRGBA.data(), _paletteRGBA.size(), &layout, &extent);
    }

    _texBG = GpuBindGroupBuilder(_gpuDevice, _texBGL)
                 .texture(0, _volumeTexGPU)
                 .texture(1, _occupancyTexGPU)
                 .texture(2, _paletteTexGPU)
                 .build();

    _dirty = false;
}

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

// ---- Execute --------------------------------------------------------------------

void MicroVoxelPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    if (!enabled || _volume.empty()) return;

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    LightComponent* light = ctx->GetMainLight();

    const glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const glm::vec3 cameraPos = camera->GetEyePosition();
    const glm::vec3 sunDir =
        light ? glm::normalize(-light->direction) : glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
    const glm::vec3 sunColor = light ? light->diffuse : glm::vec3(1.0f, 0.96f, 0.9f);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!renderer.sceneRT) return;
        if (!_pipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initGPU(dev, q, WGPUTextureFormat_RGBA16Float, static_cast<uint32_t>(renderer.sceneRT->GetNumSamples()));
        }
        if (_dirty) _uploadGPU();
        if (!_texBG) return;

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
        } u{
            invViewProj,
            viewProj,
            glm::vec4(cameraPos, 1.0f),
            glm::vec4(volumeOrigin, voxelSize),
            glm::vec4(sunDir, sunIntensity),
            glm::vec4(sunColor, ambient),
            glm::ivec4(gridDim, gridDim, gridDim, brickDim),
            glm::ivec4(maxRaySteps, shadowEnabled ? 1 : 0, 0, 0),
            glm::vec4(aoStrength, 0.0f, 0.0f, 0.0f),
        };
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

    if (_dirty) _uploadGL();

    auto [width, height] = Window::Get()->GetPhysicalSize();

    // ── GI subpass: trace one diffuse bounce per pixel and accumulate ───────
    bool giActive = giStrength > 0.0f;
    ShaderProgram* giShader = giActive ? AssetManager::Get().GetShader("microvoxel_gi") : nullptr;
    if (giActive && giShader) {
        _ensureGIRenderTargets(width, height);
        const int prev = 1 - _giCur;
        if (!_prevViewValid) {// first frame: reproject onto itself (blend rejects via alpha 0)
            _prevViewProj = viewProj;
            _prevCameraPos = cameraPos;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, _giFBOGL[_giCur]);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

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
        giShader->SetUniform(std::string("u_sunIntensity"), sunIntensity);
        giShader->SetUniform(std::string("u_frameIndex"), _giFrame);
        giShader->SetUniform(std::string("u_blend"), giBlend);
        giShader->SetUniform(std::string("u_volume"), 0);
        giShader->SetUniform(std::string("u_occupancy"), 1);
        giShader->SetUniform(std::string("u_palette"), 2);
        giShader->SetUniform(std::string("u_history"), 3);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, _volumeTexGL);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, _occupancyTexGL);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, _paletteTexGL);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, _giTexGL[prev]);

        DrawScreenQuadVAO(renderer.gl.screenQuadVAO);
        _giFrame++;
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

    shader->Activate();
    shader->SetUniform(std::string("u_invViewProj"), invViewProj);
    shader->SetUniform(std::string("u_viewProj"), viewProj);
    shader->SetUniform(std::string("u_cameraPos"), cameraPos);
    shader->SetUniform(std::string("u_volumeOrigin"), volumeOrigin);
    shader->SetUniform(std::string("u_voxelSize"), voxelSize);
    shader->SetUniform(std::string("u_gridDim"), gridDim);
    shader->SetUniform(std::string("u_brickDim"), brickDim);
    shader->SetUniform(std::string("u_maxRaySteps"), maxRaySteps);
    shader->SetUniform(std::string("u_sunDir"), sunDir);
    shader->SetUniform(std::string("u_sunColor"), sunColor);
    shader->SetUniform(std::string("u_sunIntensity"), sunIntensity);
    shader->SetUniform(std::string("u_ambient"), ambient);
    shader->SetUniform(std::string("u_shadowEnabled"), shadowEnabled ? 1 : 0);
    shader->SetUniform(std::string("u_aoStrength"), aoStrength);
    shader->SetUniform(std::string("u_giStrength"), giActive ? giStrength : 0.0f);
    shader->SetUniform(std::string("u_volume"), 0);
    shader->SetUniform(std::string("u_occupancy"), 1);
    shader->SetUniform(std::string("u_palette"), 2);
    shader->SetUniform(std::string("u_giTex"), 3);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, _volumeTexGL);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, _occupancyTexGL);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, _paletteTexGL);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, giActive ? _giTexGL[_giCur] : 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    DrawScreenQuadVAO(renderer.gl.screenQuadVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, 0);

    if (giActive) _giCur = 1 - _giCur;// ping-pong for the next frame
}

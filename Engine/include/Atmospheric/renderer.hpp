#pragma once
#include "asset_manager.hpp"
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "glm/mat4x4.hpp"
#include "globals.hpp"
#include "gpu_timer.hpp"
#include "mesh.hpp"
#include "render_target.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct GpuImageData {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channelCount = 4;
    // Raw OpenGL readback is bottom-up; set when data has NOT been row-flipped.
    bool bottomUp = false;
};

using PixelReadbackCallback = std::function<void(const GpuImageData&)>;

class GraphicsSubsystem;
class ShaderProgram;
class Renderer;
class CameraComponent;
class VoxelVolumeComponent;
class VoxelWorld;

struct RenderCommand {
    MeshHandle mesh;
    // Effective material for this draw. INVALID means "use the mesh's own
    // material" (mesh->GetMaterial()) — the historical behavior — so submitters
    // that don't override the material can leave it default. A valid handle
    // (MeshComponent's per-instance override, or a MeshInstancer's shared
    // material) now wins on the *main* render path, not only the ImGui/fallback
    // path: sort key, batch key, and every draw loop resolve this field.
    MaterialHandle material;
    // Single-instance draws: the model matrix (used for depth sort and, when no
    // instance span is attached, as the one instance). Instanced draws: the
    // cloud anchor, used only for the depth sort key.
    glm::mat4 transform;
    // Optional instance span (a MeshInstancer's whole cloud). Non-null →
    // BuildBatches appends all `instanceCount` matrices to the batch instead of
    // the lone `transform`. The pointed-to memory is owned by the submitter and
    // must outlive the frame (same contract as the per-frame command queue).
    const InstanceData* instances = nullptr;
    uint32_t instanceCount = 0;
};

// Camera/target override consumed by the scene passes (Skybox, Sun,
// VoxelChunk) so PlanarReflectionPass can re-render the world from a mirrored
// camera into an offscreen RenderTarget. Renderer::viewOverride is null while
// rendering the main view.
struct RenderViewOverride {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec3 eyePos{ 0.0f };
    RenderTarget* target = nullptr;// render here instead of sceneRT
    // World-space clip plane (n, d): fragments with dot(n, P) + d < 0 are
    // discarded. All-zero disables clipping (dot == 0 is kept).
    glm::vec4 clipPlane{ 0.0f };
    bool flipCull = false;// mirrored views reverse triangle winding
};

// The view a scene pass should actually render with: either the main camera +
// sceneRT, or PlanarReflectionPass's mirrored override. Resolved once at the
// top of each scene pass (Skybox, Sun, VoxelChunk, ForwardOpaque, WorldCanvas)
// via ResolveView so the main-view and reflection-view paths stay identical.
struct ResolvedView {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 eye;
    RenderTarget* target;
    glm::vec4 clipPlane;
    bool flipCull;
};

ResolvedView ResolveView(const Renderer& renderer, CameraComponent* camera);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#endif

class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) = 0;
};

class ShadowPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~ShadowPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue);
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUBindGroup _getOrCreateVATBG(uint32_t posTexID);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _frameUniformBuf = nullptr;// light-space viewProj
    WGPUBuffer _drawUniformBuf = nullptr;// per-draw model, dynamic offset
    uint32_t _drawSlotCapacity = 0;
    // Directional shadow map. The view is published to Renderer each frame
    // for ForwardOpaquePass to sample; this pass owns both handles.
    WGPUTexture _shadowTex = nullptr;
    WGPUTextureView _shadowView = nullptr;
    // VAT casters: displacing depth pipeline (SHADOW_VAT_WGSL). Its draw slot is
    // wider (model + vatParams), so it needs its own bind group over the shared
    // draw buffer; group 1 binds the clip's position texture.
    WGPURenderPipeline _vatPipeline = nullptr;
    WGPUBindGroupLayout _vatUniformBGL = nullptr;
    WGPUBindGroup _vatUniformBG = nullptr;
    WGPUBindGroupLayout _vatTexBGL = nullptr;
    struct CachedShadowVATBG {
        WGPUBindGroup bg = nullptr;
        WGPUTexture posTex = nullptr;
    };
    std::unordered_map<uint32_t, CachedShadowVATBG> _vatBGCache;
#endif
};

// Renders MeshType::PRIM meshes from the opaque queue. Under WebGPU, TERRAIN
// meshes are drawn too, via a non-tessellated heightmap-displacement pipeline
// mirroring the GLES/WebGL2 fallback (terrain_simple.vert + terrain.frag);
// VOXEL is handled by VoxelChunkPass.
class ForwardOpaquePass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~ForwardOpaquePass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUBindGroup _getOrCreateTexBG(uint32_t texID);
    // Group-3 bind group (VAT position+normal textures) for a clip, keyed by the
    // position texture ID; rebuilt if GfxFactory recreates either texture.
    WGPUBindGroup _getOrCreateVATBG(uint32_t posTexID, uint32_t normTexID);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    // Heightmap-displacement terrain (TERRAIN_WGSL); shares group 0/1 layouts
    // with _pipeline so _uniformBG and the texture BG cache are reused.
    WGPURenderPipeline _terrainPipeline = nullptr;
    // VAT displacement (VAT_WGSL); shares group 0/1/2 layouts with _pipeline and
    // adds group 3 (_vatBGL) for the animation textures.
    WGPURenderPipeline _vatPipeline = nullptr;
    WGPUBindGroupLayout _vatBGL = nullptr;
    struct CachedVATBG {
        WGPUBindGroup bg = nullptr;
        WGPUTexture posTex = nullptr;
        WGPUTexture normTex = nullptr;
    };
    std::unordered_map<uint32_t, CachedVATBG> _vatBGCache;
    // PRIM variant with cull=None, used when PlanarReflectionPass drives this
    // pass with a mirrored (winding-reversed) camera. Terrain already culls
    // None, so it reuses _terrainPipeline for both views.
    WGPURenderPipeline _reflPipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, camera, light) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, surface params) — dynamic-offset slot per mesh,
    // mirrors VoxelChunkPass's pattern (see voxel_chunk_pass.cpp).
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;

    // AssetManager::GetDefaultTextures() is GL-only; this pass owns its own
    // fallback texture/sampler for meshes with no base map under WebGPU.
    WGPUTexture _whiteTex = nullptr;
    WGPUSampler _sampler = nullptr;
    // `tex` records the WGPUTexture each bind group was built from so the
    // cache self-invalidates when GfxFactory::UpdateTexture2D recreates the
    // texture under the same synthetic ID (see GPUCanvasPass::CachedTexBG).
    struct CachedTexBG {
        WGPUBindGroup bg = nullptr;
        WGPUTexture tex = nullptr;
    };
    std::unordered_map<uint32_t, CachedTexBG> _texBGCache;

    // Shadow-map bind group (group 2): rebuilt only when the view published
    // by ShadowPass on the Renderer changes.
    WGPUBindGroupLayout _shadowBGL = nullptr;
    WGPUSampler _shadowSampler = nullptr;
    WGPUBindGroup _shadowBG = nullptr;
    WGPUTextureView _shadowBGSource = nullptr;
#endif
};

class DeferredGeometryPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class DeferredLightingPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// For particles, world UI
class TransparentPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class MSAAResolvePass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Screen-space GI/AO over the resolved opaque scene. Reconstructs view-space
// position and normal from the resolved depth buffer (no G-buffer needed — the
// world's voxel faces are planar, so depth-derived normals are clean), so it
// works for any geometry. Runs right after MSAAResolvePass, before transparents.
// Increment 1 is GTAO (horizon AO composited as color*AO); SSGI follows. Inert
// unless a mode is enabled. GL path only; exposed in GraphicsSubsystem panel.
class ScreenSpaceGIPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool gtaoEnabled = false;
    float gtaoStrength = 1.0f;
    float gtaoRadius = 0.5f;// view-space metres

    // SSGI: one-bounce screen-space GI (hemisphere rays marched against depth,
    // gathering bounce light from the resolved color), depth-aware blurred and
    // added to the scene. Noisy vs VoxelGI but full-resolution and needs no grid.
    bool ssgiEnabled = false;
    float ssgiStrength = 1.0f;
    float ssgiRadius = 4.0f;// view-space march distance (metres)
    float ssgiThickness = 0.5f;// depth gap counted as a hit (metres)
    float ssgiBlend = 0.9f;// temporal history weight (0 = no accumulation)
    // à-trous spatial denoise (SVGF-lite): edge-stopped by tangent-plane
    // distance + luma. 0 iterations = temporal only.
    int ssgiAtrousIters = 3;
    float ssgiSigmaDepth = 0.1f;// tangent-plane distance sigma (metres)
    float ssgiSigmaLuma = 4.0f;
    bool ssgiHalfRes = true;// trace/denoise at half res, bilinear-upsample (≈4x cheaper)

private:
    void _ensureScratch(int w, int h);
    void _ensureSSGI(int w, int h);
    GLuint _scratchFBO = 0;
    GLuint _scratchTex = 0;
    int _scratchW = 0, _scratchH = 0;
    // SSGI temporal ping-pong (rgb = accumulated indirect, a = camera distance
    // for history validation) + previous-frame reprojection state.
    GLuint _ssgiHist[2] = { 0, 0 };
    GLuint _ssgiHistFBO[2] = { 0, 0 };
    // à-trous ping-pong (display-only; never fed back into history).
    GLuint _atrousTex[2] = { 0, 0 };
    GLuint _atrousFBO[2] = { 0, 0 };
    int _ssgiW = 0, _ssgiH = 0;
    int _ssgiCur = 0;
    int _ssgiFrame = 0;// varies the per-frame sample rotation for temporal integration
    glm::mat4 _ssgiPrevVP = glm::mat4(1.0f);
    glm::vec3 _ssgiPrevEye = glm::vec3(0.0f);
    bool _ssgiPrevValid = false;
};

class WorldCanvasPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

class CanvasPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Final composite blit: single post_composite shader pass. Every effect is an
// independent toggle, applied in a fixed order (see post_composite.frag):
// UV distortion -> CA fetch -> tonemap -> LDR stylize -> gamma encode.
class PostProcessPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~PostProcessPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool tonemapEnabled = true;
    float exposure = 0.5f;

    bool caEnabled = false;
    float caStrength = 0.005f;

    bool crtEnabled = false;
    bool vhsEnabled = false;
    bool gradingEnabled = false;
    bool posterizeEnabled = false;
    bool sobelEnabled = false;
    bool edgesEnabled = false;
    bool vignetteEnabled = false;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat swapchainFormat);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUSampler _sampler = nullptr;
    // Texture bind group is recreated whenever sceneRT's texture changes
    // (e.g. on resize); cached here to avoid rebuilding it every frame.
    WGPUBindGroup _texBG = nullptr;
    WGPUTexture _texBGSource = nullptr;
#endif
};

// TODO: rename this
class UIPass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Flat billboard quad at the light source position — reads SunComponent for visual params.
class SunPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SunPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUBuffer _vertexBuf = nullptr;
#endif
};

// Gradient sky rendered at depth=1 (behind everything).  Matches VX's Skybox.
class SkyboxPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~SkyboxPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    glm::vec3 skyColor = glm::vec3(0.686f, 0.933f, 0.933f);// VX COLOR_MINT_GREEN
    glm::vec3 horizonColor = glm::vec3(1.000f, 0.980f, 0.804f);// VX COLOR_LEMON_CREAM

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUBuffer _vertexBuf = nullptr;
#endif
};

// Renders MeshType::VOXEL meshes from the opaque queue using the voxel shader.
class VoxelChunkPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~VoxelChunkPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    // Ambient occlusion for the voxel world (both default off, GL path only,
    // stackable). Corner AO is the per-vertex value baked by the greedy mesher
    // (crisp block-contact darkening); VXAO cone-traces the VoxelConeGI grid's
    // opacity for broad concavity (caves, pits) the corner term can't see — it
    // reuses the same grid VoxelGI builds (built on demand when vxaoEnabled even
    // if GI is off). Exposed in the GI panel (GraphicsSubsystem::DrawImGui).
    bool aoEnabled = false;// corner AO (baked)
    float aoStrength = 1.0f;
    bool vxaoEnabled = false;// voxel cone-traced AO
    float vxaoStrength = 1.0f;

    // Global illumination mode for the voxel world. VoxelGI cone-traces the
    // world's VoxelConeGI radiance grid (world-space, forward-friendly); SSGI is
    // screen-space (reserved — next increment). Off leaves flat ambient. Exposed
    // in the GI panel (GraphicsSubsystem::DrawImGui). GL path only.
    enum class GIMode { Off = 0, SSGI = 1, VoxelGI = 2 };
    GIMode giMode = GIMode::Off;
    float giStrength = 1.0f;// scales the indirect contribution
    int giVoxelDim = 64;// VCT cascade resolution (== world extent in metres)
    int giInjectSlabs = 4;// VCT z-slabs injected per frame (lower = smoother, no hitch)

    // Worlds whose VoxelConeGI grid this pass drives/binds for VoxelGI. Worlds
    // register themselves (VoxelWorldComponent) so the pass can reach the raw
    // voxels for CPU injection without owning the scene graph.
    void RegisterWorld(VoxelWorld* w);
    void UnregisterWorld(VoxelWorld* w);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(
        WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount, WGPUCullMode cullMode
    );
    // Grows _drawUniformBuf/_uniformBG to fit at least drawCount dynamic-offset slots.
    void _ensureDrawCapacity(uint32_t drawCount);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, light, fog, camera) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, normalMat) — one slot per mesh, selected via dynamic offset.
    // Sized lazily; all slots are written in a single call before any draw is
    // recorded each frame, since wgpuQueueWriteBuffer ordering is relative to
    // Queue::Submit, not to render-pass recording order.
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;
#endif

private:
    std::vector<VoxelWorld*> _giWorlds;
    // 1x1x1 3D texture bound to u_giRadiance whenever VoxelGI is off or no grid
    // is ready, so strict drivers (macOS) always see a valid sampler3D binding.
    GLuint _giFallbackTex = 0;
};

// Micro voxel raymarch (experimental): a fullscreen two-level DDA over a
// dense voxel volume in a 3D texture, depth-composited with the rasterized
// scene. Contrast with VoxelChunkPass, which meshes 1m macro voxels into
// triangles. Renders every registered VoxelVolumeComponent; inert when
// none are attached. See micro_voxel_pass.cpp.
class MicroVoxelPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~MicroVoxelPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    // Volume data lives in VoxelVolumeComponent; components register/unregister
    // themselves here on attach/detach. Stage 1 renders the first registered
    // volume (single-volume); the multi-volume per-object raymarch is stage 2.
    void RegisterVolume(VoxelVolumeComponent* v);
    void UnregisterVolume(VoxelVolumeComponent* v);

    // Global lighting / GI settings shared by all volumes.
    int maxRaySteps = 256;
    // Artistic gain on top of the main light's own intensity. The effective sun
    // brightness is sunIntensity * light->intensity, so the LightComponent
    // drives the level (set it on the light) and this stays a unit multiplier.
    float sunIntensity = 1.0f;
    // Ambient gain, scaled by the main light's ambient magnitude (see
    // MicroVoxelPass). The sky-hemisphere ambient shape is kept; this sets level.
    float ambient = 0.6f;
    float aoStrength = 0.7f;// Minecraft-style corner AO; 0 disables
    // Traced 1-bounce GI with temporal accumulation (GL path only for now;
    // the WebGPU path keeps the flat ambient). 0 disables and falls back to
    // the flat ambient term.
    float giStrength = 1.0f;
    float giBlend = 0.93f;// history weight per frame
    // GI trace resolution relative to the screen. Indirect light is low
    // frequency, so half res (quarter the rays) is nearly indistinguishable
    // after the bilinear-filtered composite; temporal accumulation hides the
    // rest. 1.0 = full res.
    float giResolutionScale = 0.5f;
    // Spatial GI denoiser (SVGF-lite): à-trous edge-stopping passes over the
    // temporally-accumulated GI, so 1 spp looks clean and disocclusion / post-
    // edit noise clears in one frame instead of over the temporal window. 0
    // iterations disables it (raw temporal accumulation). Sigmas tune the
    // normal / depth / luminance edge-stopping.
    int giAtrousIterations = 3;
    float giSigmaDepth = 0.5f;
    float giSigmaNormal = 64.0f;
    float giSigmaLuma = 8.0f;
    // Split-screen GI compare (observation aid): <0 disables; else the screen-x
    // split in [0,1] where the left half shows the raw temporal GI and the right
    // half the à-trous-denoised GI, so the denoiser's effect is visible in one
    // frame. The example toggles it (0.5) with B.
    float giSplitCompare = -1.0f;

    // Cross-volume GI: the GI bounce ray brute-force tests every registered
    // volume (up to kMaxGiVolumes) and keeps the nearest hit, so light bleeds
    // between volumes (A's glowstone lights B) and every volume's pixels get GI.
    // When off, GI traces only the pixel's own volume. Brute-force replaced the
    // coarse merged-global-grid approach, which lost too much detail.
    bool giCrossVolume = true;
    int debugMode = 0;// 0=off 1=albedo 2=normal 3=ao 4=shadow 5=gi 6=material
    bool shadowEnabled = true;

    // Emissive voxels: palette alpha is per-material emission strength; this
    // HDR multiplier scales it in the main pass and the GI bounce (so glowing
    // voxels also bleed indirect light onto neighbors). GL + WebGPU main pass;
    // GI pickup is GL-only.
    float emissiveStrength = 4.0f;

    // Per-material mirror reflections (palette row 1 = reflectivity/roughness):
    // one reflection ray blended by Fresnel. GL path only for now.
    bool reflectionsEnabled = true;

    // Local point lights (warm fill). GL path only for now (mirrors the GI
    // split); colors are un-scaled here and multiplied by intensity on upload.
    static constexpr int kMaxPointLights = 4;
    int pointLightCount = 0;
    glm::vec3 pointLightPos[kMaxPointLights]{};
    glm::vec3 pointLightColor[kMaxPointLights]{ glm::vec3(1.0f) };
    float pointLightIntensity[kMaxPointLights]{};
    float pointLightRadius[kMaxPointLights]{};

private:
    void _uploadGL(VoxelVolumeComponent* v);
    void _ensureGIRenderTargets(int w, int h);

    std::vector<VoxelVolumeComponent*> _volumes;

    // Per-volume GL textures (palette-index 3D + occupancy 3D + palette 2D),
    // uploaded lazily and re-uploaded when the volume's dirty flag is set
    // (regeneration or a runtime edit). Each registered volume gets its own set;
    // the main pass draws one bounding box per volume, depth-composited.
    struct GLVolume {
        GLuint volumeTex = 0;
        GLuint occupancyTex = 0;
        GLuint paletteTex = 0;
    };
    std::unordered_map<VoxelVolumeComponent*, GLVolume> _glVolumes;
    VoxelVolumeComponent* _uploadedVolume = nullptr;// WebGPU single-volume upload tracking

    // GI accumulation ping-pong (RGBA16F: rgb = indirect radiance, a = camera
    // distance for history validation)
    GLuint _giTexGL[2] = { 0, 0 };
    GLuint _giFBOGL[2] = { 0, 0 };
    // Primary-hit normal from the GI trace (MRT attachment 1, shared by both GI
    // FBOs) and the à-trous denoiser ping-pong targets — all at GI resolution.
    GLuint _giNormalTexGL = 0;
    GLuint _atrousTexGL[2] = { 0, 0 };
    GLuint _atrousFBOGL[2] = { 0, 0 };

    int _giW = 0, _giH = 0;
    int _giCur = 0;
    int _giFrame = 0;
    glm::mat4 _prevViewProj{ 1.0f };
    glm::vec3 _prevCameraPos{ 0.0f };
    bool _prevViewValid = false;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    void _uploadGPU(VoxelVolumeComponent* v);
    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    WGPUBindGroup _texBG = nullptr;
    WGPUBuffer _uniformBuf = nullptr;
    WGPUTexture _volumeTexGPU = nullptr;
    WGPUTexture _occupancyTexGPU = nullptr;
    WGPUTexture _paletteTexGPU = nullptr;
#endif
};

// Renders MeshType::PRIM water meshes tagged via material renderQueue.
class WaterPass : public RenderPass {
public:
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    ~WaterPass() override;
#endif
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    float time = 0.0f;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
private:
    void _initGPU(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat colorFormat, uint32_t sampleCount);
    void _ensureDrawCapacity(uint32_t drawCount);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroup _uniformBG = nullptr;
    // Frame-constant data (viewProj, camera, light, time) — one fixed-size binding.
    WGPUBuffer _frameUniformBuf = nullptr;
    // Per-draw data (model, water shading params) — dynamic-offset slot per
    // mesh, mirrors ForwardOpaquePass/VoxelChunkPass's pattern.
    WGPUBuffer _drawUniformBuf = nullptr;
    uint32_t _drawSlotCapacity = 0;

    // Planar-reflection texture (group 1). When PlanarReflectionPass is
    // inactive a 1x1 black fallback is bound instead; the per-draw
    // reflection strength is zeroed so the sample is never visible. The bind
    // group is rebuilt whenever the bound WGPUTexture changes (RT resize).
    WGPUBindGroupLayout _reflBGL = nullptr;
    WGPUSampler _reflSampler = nullptr;
    WGPUTexture _reflFallbackTex = nullptr;
    WGPUBindGroup _reflBG = nullptr;
    WGPUTexture _reflBGSource = nullptr;
#endif
};

// Re-renders the world (Skybox + Sun + VoxelChunk content) mirrored about a
// reflective material's plane into an offscreen RenderTarget. Consumed by
// WaterPass today; a future mirror surface only needs planarReflection=true
// on its material plus a shader that samples GetReflectionRT().
//
// The reflection plane is derived from the reflective object's transform:
// the plane through its position with its up (+Y) axis as normal. One plane
// is supported per frame — the first draw whose material opts in wins.
// PRIM opaque meshes (ForwardOpaquePass content) are not yet re-rendered.
class PlanarReflectionPass : public RenderPass {
public:
    PlanarReflectionPass();
    ~PlanarReflectionPass() override;
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    // Reflection RT resolution relative to the window's physical size.
    float resolutionScale = 0.5f;

    // True when a reflection was rendered this frame (reflective material
    // found and the camera is on the reflective side of the plane).
    bool IsActive() const {
        return _active;
    }
    RenderTarget* GetReflectionRT() const {
        return _rt.get();
    }
    // proj * mirroredView of the frame this RT was rendered with; project a
    // world position with it to get the RT sample position in clip space.
    const glm::mat4& GetReflectionViewProj() const {
        return _reflViewProj;
    }

private:
    std::unique_ptr<RenderTarget> _rt;
    glm::mat4 _reflViewProj{ 1.0f };
    bool _active = false;

    // Private sub-pass instances rather than the RenderGraph's: under WebGPU
    // every pass writes its single per-frame uniform buffer via
    // wgpuQueueWriteBuffer, whose ordering is relative to Queue::Submit — so
    // executing the graph's own instances a second time per frame would make
    // both views observe only the last-written uniforms. Separate instances
    // own separate buffers (and, for VoxelChunkPass, a front-face-culling
    // pipeline for the mirrored winding).
    std::unique_ptr<ForwardOpaquePass> _forward;
    std::unique_ptr<SkyboxPass> _skybox;
    std::unique_ptr<SunPass> _sun;
    std::unique_ptr<VoxelChunkPass> _voxel;
    std::unique_ptr<WorldCanvasPass> _worldCanvas;
};

class PortalMaterial;

// Recursive "through the portal" views for every linked PortalMaterial pair
// found in the transparent queue (see PortalMaterial in material.hpp).
//
// Per visible portal surface s (looking into s shows the world seen from
// behind its partner), a chain of virtual cameras is rendered depth-first:
// rts[i] holds the image after i+1 portal transits, each level clipped at the
// partner's plane and drawn with the world sub-passes (ForwardOpaque, Skybox,
// Sun, VoxelChunk) plus the portal surfaces themselves sampling one level
// deeper — that nesting is what produces the infinite-corridor recursion.
// The recursion floor (level == recursionDepth) paints the void fill.
//
// Water and world sprites are not re-rendered inside portal views yet (water
// depends on the main view's resolve/reflection chain; sprites share
// GPUCanvasPass's per-variant uniform slots on WebGPU).
//
// Like PlanarReflectionPass, this is the hardcoded precursor of a future
// generic AuxViewPass; see that class's comment for the WebGPU
// one-uniform-write-per-frame rationale behind the private sub-pass copies.
class PortalPass : public RenderPass {
public:
    PortalPass();
    ~PortalPass() override;
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    // Platform-scaled budget defaults, set in the constructor: handheld GPUs
    // (iOS/Android, tile-based) get a single hop and a smaller RT since each
    // recursion level is a full offscreen scene re-render. Any scene using
    // portals inherits these — the per-platform choice lives here, not in the
    // examples. Override after construction for a per-scene budget.
    int recursionDepth = 4;// portal-in-portal levels before the void floor (desktop)
    float resolutionScale = 0.4f;// portal RT size relative to the window

    // viewProj of every portal recursion level rendered last frame. Published
    // so the submission side can cull against these instead of disabling
    // culling wholesale (see Renderer::GetAuxViewProjs). One frame stale —
    // submission runs before Execute — which is fine for slow-moving portals.
    const std::vector<glm::mat4>& ActiveViewProjs() const {
        return _activeViewProjs;
    }

    struct PortalView {
        PortalMaterial* mat = nullptr;// identity key
        Mesh* mesh = nullptr;// surface mesh, refreshed each frame from the queue
        glm::mat4 surfaceTransform{ 1.0f };
        bool active = false;// linked and its chain was rendered this frame
        bool seenThisFrame = false;// surface draw found in the queue this frame
        std::vector<std::unique_ptr<RenderTarget>> rts;// rts[i] = image after i+1 transits
    };

    bool AnyActive() const {
        return _anyActive;
    }

    // Draws every portal surface found this frame into the currently resolved
    // target (main view or a recursion RT). sampleLevel picks the recursion
    // image the surfaces show: 0 in the main view, k inside rts[k-1];
    // >= recursionDepth (or an inactive surface) paints the void fill.
    // `exclude` skips the portal being looked through (the virtual camera
    // sits just behind it — drawing it would curtain the whole view).
    void DrawPortalSurfaces(
        GraphicsSubsystem* ctx,
        Renderer& renderer,
        CommandEncoder* enc,
        int sampleLevel,
        const Material* exclude = nullptr
    );

private:
    // Per (portal, recursion level) world sub-pass copies — same WebGPU
    // uniform-write rationale as PlanarReflectionPass's private instances.
    struct WorldReplay {
        std::unique_ptr<ForwardOpaquePass> forward;
        std::unique_ptr<SkyboxPass> skybox;
        std::unique_ptr<SunPass> sun;
        std::unique_ptr<VoxelChunkPass> voxel;
        std::unique_ptr<WaterPass> water;// simplified (depth-less, no reflection) inside portals
    };
    WorldReplay& _replaySlot(size_t index);

    std::vector<std::unique_ptr<WorldReplay>> _replays;
    std::vector<PortalView> _views;
    std::vector<glm::mat4> _activeViewProjs;// one per rendered recursion level
    bool _anyActive = false;

    // GL: 1x1 black texture bound while a surface shows the void — sampling an
    // unbound unit is undefined and trips macOS's "texture unloadable" warning.
    GLuint _glFallbackTex = 0;

    PortalView& _viewFor(PortalMaterial* mat);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // Portal-surface drawing state. One dynamic-offset slot per surface draw,
    // consumed across all invocations of DrawPortalSurfaces in a frame (the
    // cursor resets in Execute); one texture bind group per sampled RT.
    void _initSurfaceGPU(WGPUDevice device, WGPUQueue queue, uint32_t sampleCount);
    void _ensureSurfCapacity(uint32_t slotCount);
    WGPUBindGroup _getOrCreateSurfTexBG(WGPUTexture tex);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPURenderPipeline _surfPipeline = nullptr;
    WGPUBindGroupLayout _surfUniformBGL = nullptr;
    WGPUBindGroupLayout _surfTexBGL = nullptr;
    WGPUBindGroup _surfUniformBG = nullptr;
    WGPUBuffer _surfDrawBuf = nullptr;// dynamic-offset slots
    uint32_t _surfSlotCapacity = 0;
    uint32_t _surfSlotCursor = 0;
    WGPUSampler _surfSampler = nullptr;
    WGPUTexture _surfFallbackTex = nullptr;// bound when a surface shows the void
    struct CachedSurfTexBG {
        WGPUBindGroup bg = nullptr;
        WGPUTexture tex = nullptr;
    };
    std::unordered_map<uintptr_t, CachedSurfTexBG> _surfTexBGCache;
#endif
};

// Draws the portal surfaces into the main view (PortalPass::DrawPortalSurfaces
// with sampleLevel 0). Runs after the opaque world passes, before MSAA
// resolve, so portals are depth-tested/-written like solid geometry.
class PortalSurfacePass : public RenderPass {
public:
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;
};

// Pyramid bloom: threshold → downsample → upsample → composite.
class BloomPass : public RenderPass {
public:
    ~BloomPass() override;
    void Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr) override;

    bool enabled = false;
    float threshold = 1.0f;// VX: brightness.frag hard cutoff
    float bloomStrength = 0.08f;// VX: u_bloom_strength

private:
    static constexpr int MIP_LEVELS = 5;

    struct MipRT {
        GLuint fbo = 0;
        GLuint tex = 0;
        int w = 0;
        int h = 0;
    };

    MipRT _mips[MIP_LEVELS];
    GLuint _tempFBO = 0;
    GLuint _tempTex = 0;
    bool _initialized = false;
    int _lastW = 0, _lastH = 0;

    void InitMips(int w, int h);
    void DrawScreenQuad(GLuint screenVAO);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // WebGPU bloom mirrors the GL path exactly: a MIP_LEVELS mip pyramid with
    // threshold -> downsample chain -> additive upsample chain -> composite.
    // Avoids reading sceneRT's own texture while it's bound as the composite
    // pass's render attachment by first copying it into _snapshotTex (requires
    // CopySrc on sceneRT's texture, see gpu_render_target.cpp); the threshold
    // pass samples that same snapshot.
    void _initGPU(WGPUDevice device, WGPUQueue queue, uint32_t sceneSampleCount);
    void _resizeGPU(int width, int height);

    WGPUDevice _gpuDevice = nullptr;
    WGPUQueue _gpuQueue = nullptr;
    WGPUSampler _sampler = nullptr;

    WGPURenderPipeline _threshPipeline = nullptr;
    WGPURenderPipeline _downPipeline = nullptr;
    WGPURenderPipeline _upPipeline = nullptr;// additive blend
    WGPURenderPipeline _compPipeline = nullptr;

    WGPUBindGroupLayout _threshBGL = nullptr;
    WGPUBindGroupLayout _downBGL = nullptr;
    WGPUBindGroupLayout _upBGL = nullptr;
    WGPUBindGroupLayout _compBGL = nullptr;

    WGPUBuffer _threshUniformBuf = nullptr;// threshold, rewritten each frame
    WGPUBuffer _compUniformBuf = nullptr;// bloomStrength, rewritten each frame
    // One texelSize uniform per down/up step; content depends only on the mip
    // sizes, so written once per resize. Indices 1..MIP_LEVELS-1 are used.
    WGPUBuffer _downUniformBuf[MIP_LEVELS] = {};
    WGPUBuffer _upUniformBuf[MIP_LEVELS] = {};

    // GPU mip chain mirroring the GL _mips pyramid; level 0 is half-res, each
    // subsequent level halves again. Level 0 also holds the final accumulated
    // bloom after the upsample chain (what the composite samples).
    struct GpuMip {
        WGPUTexture tex = nullptr;
        WGPUTextureView view = nullptr;
        int w = 0, h = 0;
    };
    GpuMip _gpuMips[MIP_LEVELS];

    // Full-resolution copy of sceneRT, refreshed each frame before composite.
    WGPUTexture _snapshotTex = nullptr;
    WGPUTextureView _snapshotView = nullptr;

    WGPUBindGroup _threshBG = nullptr;// reads _snapshotTex, writes mip[0]
    WGPUBindGroup _downBG[MIP_LEVELS] = {};// _downBG[i] reads mip[i-1], writes mip[i]
    WGPUBindGroup _upBG[MIP_LEVELS] = {};// _upBG[i] reads mip[i], writes mip[i-1]
    WGPUBindGroup _compBG = nullptr;// reads _snapshotTex + mip[0]

    int _gpuWidth = 0, _gpuHeight = 0;

    void _destroyGPUTextures();
#endif
};

class RenderGraph {
    struct PassEntry {
        std::unique_ptr<RenderPass> pass;
        GpuTimer timer;
        std::string name;
    };
    std::vector<PassEntry> _entries;

public:
    // Off by default — GL_TIMESTAMP queries add real overhead (driver ordering
    // barriers around pass boundaries) even though readback is async.
    // Enable only when the timing panel is in use.
    bool gpuProfilingEnabled = false;

    ~RenderGraph();

    void AddPass(std::unique_ptr<RenderPass> pass);
    void Render(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc = nullptr);

    // Returns {passName, gpuMs} for every pass (one frame behind, stall-free).
    std::vector<std::pair<std::string, float>> GetTimings() const;

    template<typename T> T* GetPass() {
        for (auto& e : _entries) {
            if (auto* t = dynamic_cast<T*>(e.pass.get())) return t;
        }
        return nullptr;
    }
};

class Renderer {
public:
    enum class RenderPath { Forward, Deferred };

    struct RenderTargetProps {
        int width = INIT_FRAMEBUFFER_WIDTH;
        int height = INIT_FRAMEBUFFER_HEIGHT;
        int numSamples = MSAA_NUM_SAMPLES;
    };

    Renderer() = default;
    ~Renderer() = default;

    void Init(int width, int height);
    void Cleanup();
    void Resize(int width, int height);

    void CheckFramebufferStatus(const std::string& prefix);
    void CheckErrors(const std::string& prefix);

    void PushDebugLine(DebugVertex from, DebugVertex to);

    void EnableWireframe(bool enable = true) {
        wireframeEnabled = enable;
    }

    template<typename T> T* GetPass() {
        return _renderGraph ? _renderGraph->GetPass<T>() : nullptr;
    }

    // True when an auxiliary view whose frustum can differ arbitrarily from
    // the main camera's is being rendered (portals, as of last frame) — the
    // submission side then skips frustum culling so aux views see the whole
    // scene. The mirrored reflection view is deliberately not included: its
    // frustum mostly overlaps the main one and always-on water would disable
    // culling permanently.
    // View-projection matrices of every auxiliary view rendered last frame
    // (portal recursion levels + the water reflection). The submission side
    // keeps any object visible in the main frustum OR any of these, so aux
    // views see the whole scene without disabling frustum culling wholesale.
    // Empty when no aux views are active. One frame stale by construction
    // (submission precedes the passes) — fine for slow-moving portals/water.
    std::vector<glm::mat4> GetAuxViewProjs();

    std::vector<std::pair<std::string, float>> GetTimings() const {
        return _renderGraph ? _renderGraph->GetTimings() : std::vector<std::pair<std::string, float>>{};
    }

#ifdef AE_GPU_TIMER_ENABLED
    bool& GpuProfilingEnabled() {
        return _renderGraph->gpuProfilingEnabled;
    }
#endif

    void SetCapability(const GLenum& cap, bool enable = true) {
        if (enable) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    void SubmitCommand(const RenderCommand& cmd);
    void RenderFrame(GraphicsSubsystem* ctx, float dt);

    void BeginTransformFeedbackPass();
    void BindTransformFeedbackBuffer(GLuint bufferId, GLuint index = 0);
    void EndTransformFeedbackPass();

    auto& GetOpaqueQueue() {
        return _opaqueQueue;
    }
    auto& GetTransparentQueue() {
        return _transparentQueue;
    }

    void SubmitUICommand(const BatchDrawCommand& cmd);

    auto& GetUIQueue() {
        return _hudQueue;
    }

    void SubmitCanvasCommand(const BatchDrawCommand& cmd);
    auto& GetCanvasQueue() {
        return _canvasQueue;
    }

    // Read rendered pixels asynchronously. On this OpenGL backend the readback
    // is actually synchronous; the callback fires before this method returns.
    // Pixels are in RGBA order, top-to-bottom (flipped from the raw GL output).
    void readPixelsAsync(PixelReadbackCallback callback);

    // Two-phase async readback for high-throughput video recording.
    // Both must be called each frame from the render/GL thread.
    //
    // Phase 1 – issue this frame's DMA into the next PBO slot (non-blocking).
    void schedulePixelReadback();
    // Phase 2 – map the PBO written 2 frames ago and return its pixel data.
    // Returns nullopt while the pipeline is priming (first 2 frames) or if
    // no readback has been scheduled. GpuImageData.bottomUp is true; move
    // GpuImageData.data into the destination to avoid a second memcpy.
    std::optional<GpuImageData> collectPixelReadback();
    // Release PBO and FBO resources. Call when recording stops or on Cleanup.
    void destroyReadbackPBOs();

    glm::vec4 clearColor = glm::vec4(0.15f, 0.183f, 0.2f, 1.0f);
    bool wireframeEnabled = false;

    // Equirectangular HDR environment map (RGBA16F, from AssetManager::LoadHDR).
    // When valid, SkyboxPass renders it as the sky (and later IBL phases source
    // irradiance/reflection from it); invalid falls back to the gradient sky.
    TextureHandle environmentMap;
    // Top mip level of environmentMap, used by the PBR IBL term as the fully
    // blurred (diffuse / roughest specular) sample. GL clamps textureLod beyond
    // the real mip count, so a generous default is safe for any map size.
    float environmentMaxLod = 10.0f;

    // Non-null only while PlanarReflectionPass drives the scene passes with a
    // mirrored camera; passes fall back to the main camera + sceneRT when null.
    const RenderViewOverride* viewOverride = nullptr;

    // Abstract render targets (backend-independent)
    std::unique_ptr<RenderTarget> sceneRT;
    std::unique_ptr<RenderTarget> msaaResolveRT;

    // Abstract buffers (backend-independent)
    std::unique_ptr<Buffer> debugBuffer;
    std::unique_ptr<Buffer> screenBuffer;

    // All OpenGL-specific state grouped here; WebGPU paths never touch these fields.
    struct GL {
        GLuint finalFBO = 0;
        std::array<GLuint, MAX_UNI_LIGHTS> uniShadowMaps = {};
        std::array<GLuint, MAX_OMNI_LIGHTS> omniShadowMaps = {};
        GLuint envMap = 0, irradianceMap = 0;
        struct GBuffer {
            GLuint id = 0;
            GLuint positionRT = 0;
            GLuint normalRT = 0;
            GLuint albedoRT = 0;
            GLuint materialRT = 0;
            GLuint depthRT = 0;
        } gBuffer;
        GLuint shadowFBO = 0;
        GLuint canvasVAO = 0, canvasVBO = 0;
        GLuint screenQuadVAO = 0;
        GLuint skyboxVAO = 0, skyboxVBO = 0;
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        GLuint glesResolvedDepthTex = 0;
        GLuint glesResolvedDepthFBO = 0;
        int glesResolvedDepthWidth = 0;
        int glesResolvedDepthHeight = 0;
#endif
    } gl;

    // Per-frame time (seconds) forwarded from RenderFrame for animated passes.
    float frameTime = 0.0f;

    // Returns the resolved (non-MSAA) depth texture for screen-space effects.
    GLuint GetResolvedDepthTexture() const {
#if defined(__EMSCRIPTEN__) || defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        // WebGL 2.0 does not allow reading from the depth texture of the bound FBO (feedback loop).
        // Since sceneRT is single-sampled on WebGL, we can read from sceneRT's depth texture instead!
        // But if sceneRT is multi-sampled (MSAA enabled), sceneRT has no depth texture (returns 0),
        // so we must read from the glesResolvedDepthTex where we resolved the depth.
        if (sceneRT && sceneRT->GetNumSamples() > 1) {
            return gl.glesResolvedDepthTex;
        }
        if (!sceneRT) return 0;
        return static_cast<GLuint>(sceneRT->GetDepthTextureID());
#else
        if (!msaaResolveRT) return 0;
        return static_cast<GLuint>(msaaResolveRT->GetDepthTextureID());
#endif
    }

    struct SortableCommand {
        RenderCommand cmd;
        uint64_t sortKey;
    };

private:
    std::vector<RenderCommand> _commandList;

    // Bucketed and sorted queues
    std::vector<SortableCommand> _opaqueQueue;// scene, voxel chunks, skybox
    std::vector<SortableCommand> _afterOpaqueQueue;// raymarching, GPU particles
    std::vector<SortableCommand> _transparentQueue;// particles, world UI
    std::vector<SortableCommand> _gizmoQueue;// world debug UI
    std::vector<BatchDrawCommand> _hudQueue;// HUD (RmlUi)
    std::vector<BatchDrawCommand> _canvasQueue;// immediate mode canvas (Lua)

    std::unique_ptr<RenderGraph> _renderGraph;
    RenderPath _currRenderPath = RenderPath::Forward;

    void CreateFBOs();
    void DestroyFBOs();
    void CreateRTs(const RenderTargetProps& props);
    void DestroyRTs();

    void CreateCanvasVAO();
    void CreateScreenBuffer();
    void CreateDebugBuffer();
    void CreateScreenQuadVAO();

    // Async readback state (3-PBO ring buffer + persistent FBO).
    static constexpr int READBACK_PBO_COUNT = 3;
    GLuint m_readbackPBOs[READBACK_PBO_COUNT] = {};
    GLuint m_readbackFBO = 0;
    uint32_t m_readbackFrameIdx = 0;
    uint32_t m_readbackPBOWidth = 0;
    uint32_t m_readbackPBOHeight = 0;

    void SortAndBucket(const glm::vec3& cameraPos);
    uint64_t CalculateSortKey(const RenderCommand& cmd, const glm::vec3& cameraPos);
    void BucketCommands(const glm::vec3& cameraPos);
    void SortOpaque();
    void SortTransparent();

    std::unique_ptr<BatchRenderer2D> m_BatchRenderer;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    std::unique_ptr<GPUCanvasPass> m_GPUCanvasPass;
#endif

public:
    BatchRenderer2D* GetBatchRenderer() const {
        return m_BatchRenderer.get();
    }

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    GPUCanvasPass* GetGPUCanvasPass() const {
        return m_GPUCanvasPass.get();
    }

    // Shadow-map handoff: written by ShadowPass each frame (non-owning view —
    // ShadowPass owns the texture), consumed by ForwardOpaquePass. The light
    // VP already includes the GL→WebGPU [-1,1]→[0,1] depth-range fixup, so
    // consumers use it as-is for textureSampleCompare.
    WGPUTextureView wgpuShadowMapView = nullptr;
    glm::mat4 wgpuShadowLightVP = glm::mat4(1.0f);
#endif
};

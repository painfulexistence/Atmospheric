#pragma once
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "globals.hpp"
#include <cstddef>
#include <functional>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <limits>

enum class PolygonMode { Fill, Line, Point };

enum class RenderQueue {
    Background = 1000,// Skybox, far background
    Opaque = 2000,// Normal opaque objects
    AlphaTest = 2450,// Objects with alpha testing (vegetation, etc.)
    Transparent = 3000,// Transparent objects (glass, particles, etc.)
    Overlay = 4000// UI, HUD, debug overlays
};

enum class CullMode {
    None,// Draw both faces
    Front,// Cull front-facing triangles
    Back,// Cull back-facing triangles (default 3D)
};

enum class CompareFunc {
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    NotEqual,
    Always,
};

struct DepthState {
    bool testEnabled = true;
    bool writeEnabled = true;
    CompareFunc compare = CompareFunc::Less;

    bool operator==(const DepthState& o) const {
        return testEnabled == o.testEnabled && writeEnabled == o.writeEnabled && compare == o.compare;
    }
};

// Consolidated render-state description. Lives on Material; pipelines for
// batched draws (GPUCanvasPass) key their cache on RenderState::Hash().
struct RenderState {
    BlendMode blend = BlendMode::None;
    CullMode cull = CullMode::Back;
    DepthState depth = {};
    PolygonMode polygon = PolygonMode::Fill;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;

    bool operator==(const RenderState& o) const {
        return blend == o.blend && cull == o.cull && depth == o.depth && polygon == o.polygon && topology == o.topology;
    }

    // Hash for pipeline cache dedup. Not cryptographic — just a spread of
    // the enum fields into a size_t so `unordered_map<RenderState, ...>` is
    // cheap and distinct states rarely collide.
    size_t Hash() const {
        size_t h = 0;
        auto mix = [&h](size_t v) { h = h * 1315423911u + v; };
        mix(static_cast<size_t>(blend));
        mix(static_cast<size_t>(cull));
        mix(static_cast<size_t>(depth.testEnabled) | (static_cast<size_t>(depth.writeEnabled) << 1));
        mix(static_cast<size_t>(depth.compare));
        mix(static_cast<size_t>(polygon));
        mix(static_cast<size_t>(topology));
        return h;
    }
};

// Interchange data-bag for every material kind (JSON scenes, importers, script
// bindings all fill this). It is a superset — a given Material subclass reads
// only the fields it understands (PBRMaterial ignores specular/shininess,
// BlinnPhongMaterial ignores roughness/metallic, etc.).
struct MaterialProps {
    TextureHandle baseMap;
    TextureHandle normalMap;
    TextureHandle aoMap;
    TextureHandle roughnessMap;
    TextureHandle metallicMap;
    TextureHandle heightMap;// TerrainMaterial only
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);
    // PBR: value = map * factor; with no map bound the factor stands alone
    // (absent map counts as white, glTF semantics). Engine defaults are a
    // fully-rough dielectric; importers overwrite both from the source file,
    // so glTF/USD assets keep their spec defaults.
    float roughnessFactor = 1.0f;
    float metallicFactor = 0.0f;
    glm::vec3 specular = glm::vec3(.7, .7, .7);// BlinnPhong only
    glm::vec3 ambient = glm::vec3(0, 0, 0);// BlinnPhong only
    float shininess = .25;// BlinnPhong only
    bool cullFaceEnabled = true;
    PrimitiveTopology primitiveType = PrimitiveTopology::Triangles;
    PolygonMode polygonMode = PolygonMode::Fill;
};

// Base surface material. Deliberately shading-model-agnostic: only the inputs
// that describe the surface itself — the albedo tint, its texture and a normal
// map — live here, because every lit model (PBR, Blinn-Phong, …) consumes the
// same albedo + normal in the same way. Everything that encodes light response
// belongs to the leaf types: ambient occlusion, metallic/roughness maps+factors
// and the transmission set on PBRMaterial; the specular/shininess Phong terms
// on BlinnPhongMaterial; heightMap on TerrainMaterial. Scenes and importers
// create PBRMaterial (the engine default) via AssetManager::CreateMaterial.
class Material {
public:
    TextureHandle baseMap;
    TextureHandle normalMap;
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);

    // Consolidated render-state: blend, cull, depth, polygon, topology.
    // MaterialProps still exposes cullFaceEnabled/primitiveType/polygonMode
    // for JSON scene compatibility; the constructor maps those into
    // renderState. Direct callers should read/write renderState only.
    RenderState renderState;

    RenderQueue renderQueue = RenderQueue::Opaque;
    int renderQueueOffset = 0;// Fine-tune rendering order within queue

    // ── Planar reflection (opt-in) ──────────────────────────────────────────
    // When enabled, PlanarReflectionPass re-renders the scene mirrored about
    // this object's plane — the plane through the object's position with the
    // object transform's up (+Y) axis as its normal — into an offscreen
    // RenderTarget the surface shader then samples. This lives on the base
    // Material (not WaterMaterial) so any flat surface can become reflective:
    // water today, a wall mirror later is just planarReflection=true plus a
    // shader that samples PlanarReflectionPass's RT. The reflection RT itself
    // is owned by the pass, never by a material (materials are shareable
    // value-ish objects; GPU targets are per-backend resources).
    // Current pass limitation: one reflection plane per frame (the first
    // enabled draw wins); see PlanarReflectionPass.
    bool planarReflection = false;
    float reflectionStrength = 0.6f;// blend weight toward the mirror image at grazing angles
    float reflectionDistortion = 0.02f;// normal-driven UV wobble (waves); 0 for a perfect mirror

    int GetFinalRenderQueue() const {
        return static_cast<int>(renderQueue) + renderQueueOffset;
    }

    virtual ~Material() = default;

    // Owned material inspector. Base class draws the shared surface fields
    // (maps, diffuse, cull mode); subclasses call the base and add their own
    // controls so per-type tunables (factors, Phong terms, palette, wave
    // strength, etc.) live with the material instead of leaking into every
    // component's DrawImGui.
    virtual void DrawImGui();

    Material(const MaterialProps& props) {
        baseMap = props.baseMap;
        normalMap = props.normalMap;
        diffuse = props.diffuse;
        renderState.cull = props.cullFaceEnabled ? CullMode::Back : CullMode::None;
        renderState.topology = props.primitiveType;
        renderState.polygon = props.polygonMode;
    }
};

// The default surface shading model (metallic/roughness, Cook-Torrance in
// pbr.frag). Base for most materials — every importer and JSON scene produces
// one via AssetManager::CreateMaterial. Final roughness/metalness is
// map-sample * factor (glTF semantics), so with no maps bound the factors
// stand alone and no 1x1 solid texture is needed.
class PBRMaterial : public Material {
public:
    TextureHandle aoMap;// ambient occlusion — a PBR-pipeline input (modulates indirect light)
    TextureHandle roughnessMap;
    TextureHandle metallicMap;
    float roughnessFactor = 1.0f;
    float metallicFactor = 0.0f;

    // ── Transmission / volume / IOR (populated from glTF KHR_materials_*) ───
    // Data carried through from import for a future refraction/transmission
    // pass; no current shader reads these. Defaults = opaque thin-walled
    // dielectric, so existing materials render unchanged. See PrefabMaterial
    // for field meanings.
    float transmissionFactor = 0.0f;// 0 = opaque; 1 = fully transmissive
    TextureHandle transmissionMap;// R channel scales transmissionFactor
    float ior = 1.5f;// index of refraction
    float thicknessFactor = 0.0f;// 0 = thin-walled; >0 = has a volume
    TextureHandle thicknessMap;// G channel scales thicknessFactor
    float attenuationDistance = std::numeric_limits<float>::infinity();// +inf = no absorption
    glm::vec3 attenuationColor = glm::vec3(1.0f);// Beer-Lambert tint through the volume

    explicit PBRMaterial(const MaterialProps& props = MaterialProps{}) : Material(props) {
        aoMap = props.aoMap;
        roughnessMap = props.roughnessMap;
        metallicMap = props.metallicMap;
        roughnessFactor = props.roughnessFactor;
        metallicFactor = props.metallicFactor;
    }

    void DrawImGui() override;
};

// Legacy Blinn-Phong shading (diffuse/specular/ambient/shininess, blinnphong
// shader). Opt-in alternative to PBRMaterial for stylised or retro surfaces.
class BlinnPhongMaterial : public Material {
public:
    glm::vec3 specular = glm::vec3(.7, .7, .7);
    glm::vec3 ambient = glm::vec3(0, 0, 0);
    float shininess = .25f;

    explicit BlinnPhongMaterial(const MaterialProps& props = MaterialProps{}) : Material(props) {
        specular = props.specular;
        ambient = props.ambient;
        shininess = props.shininess;
    }

    void DrawImGui() override;
};

class WaterMaterial : public Material {
public:
    float waterLine = 32.0f;
    float waveStrength = 0.1f;
    float waveSpeed = 1.0f;
    // Fog color is not stored here -- WaterPass reads it live from SkyboxPass::skyColor,
    // matching VX (chunk/water fog both track the sky gradient color every frame).
    float waterFogDensity = 0.00001f;// VX: u_fog_density in scene.py render_water/render_terrain
    glm::vec3 deepColor = { 0.05f, 0.1f, 0.25f };// VX COLOR_INDIGO
    glm::vec3 shallowColor = { 0.686f, 0.933f, 0.933f };// VX COLOR_MINT_GREEN
    float beerCoef = 0.095f;

    WaterMaterial() : Material(MaterialProps{}) {
        renderQueue = RenderQueue::Transparent;
        renderState.cull = CullMode::None;
        planarReflection = true;// water reflects the sky/terrain by default
    }
};

// A "through the portal" window surface. PortalPass finds every PortalMaterial
// draw in the transparent queue, pairs it with its partner, and renders the
// recursive views the surface then samples (see PortalPass in renderer.hpp).
// The disc mesh's local frame defines the portal: +Z faces out (the side you
// look into / exit from), +Y is up. Pair two portals via PortalComponent::Link,
// which sets `partner` on both materials.
//
// Sits in the Transparent queue only to stay out of ForwardOpaquePass's PRIM
// drawing; WaterPass explicitly skips PortalMaterial and PortalSurfacePass
// draws it (opaque, depth-written).
class PortalMaterial : public Material {
public:
    PortalMaterial* partner = nullptr;// the linked exit portal; null = unlinked (renders as void)
    glm::vec3 rimColor = { 0.35f, 0.65f, 1.0f };// HDR-scaled edge glow (feeds bloom)

    PortalMaterial() : Material(MaterialProps{}) {
        renderQueue = RenderQueue::Transparent;
        renderState.cull = CullMode::None;// visible (as void) from behind too
    }
};

class VATClip;

// Material for a mesh whose vertices are displaced by a Vertex Animation Texture
// (see vat.hpp). Carries the clip (non-owning — the AnimationLibrary owns it,
// pointed at by the driving VATComponent) and the current normalized playhead
// the renderer feeds to vat.vert. Rendered by the standard opaque pass with the
// "vat" shader instead of "color"; all the usual surface/texture fields are
// inherited and still apply. Reuses pbr.frag, so it is a PBRMaterial (inherits
// the roughness/metallic factors too).
class VATMaterial : public PBRMaterial {
public:
    // Non-owning; the AnimationLibrary clip outlives the material's use in a draw.
    VATClip* clip = nullptr;
    float normalizedTime = 0.0f;// playhead in [0, 1], advanced by VATComponent

    VATMaterial() : PBRMaterial(MaterialProps{}) {
    }
    explicit VATMaterial(const MaterialProps& props) : PBRMaterial(props) {
    }
};

// One detail texture layer of a terrain, blended by splat-map weight (or by
// the automatic slope/height weights when no splat map is set).
struct TerrainLayer {
    TextureHandle albedoMap;
    TextureHandle normalMap;// optional tangent-space detail normal
    float tiling = 32.0f;// texture repeats across the whole terrain
};

// Terrain surface description. Designed around WorldCreator/Gaea exports:
//   heightMap             — 16-bit displacement map (baked by TerrainMeshComponent)
//   baseMap   (Material)  — full-terrain color/texture map, 0-1 UV
//   normalMap (Material)  — full-terrain normal map (overrides heightmap-derived normals)
//   aoMap     (PBR)       — full-terrain ambient occlusion
//   splatMap              — RGBA weight masks for up to 4 detail layers
//                           (Gaea flow/wear/deposit masks packed into channels)
// All maps are optional; with none set the shader falls back to the legacy
// height-palette coloring (now lit).
// Streamed grass blades (see TerrainStreamer's grass ring + grass.vert/.frag).
// One shared instance covers every grass cell: per-blade variation lives in
// vertex attributes, so the material only carries the look (root->tip color
// gradient), the wind field, the ring fade, and fog matching the terrain.
class GrassMaterial : public Material {
public:
    glm::vec3 rootColor = glm::vec3(0.24f, 0.17f, 0.07f);// dark base of the blade
    glm::vec3 tipColor = glm::vec3(0.93f, 0.76f, 0.38f);// sun-lit golden tip
    glm::vec2 windDir = glm::vec2(0.8f, 0.6f);// XZ, normalized in the shader
    float windStrength = 0.35f;// horizontal push as a fraction of blade height
    float windSpeed = 1.6f;// gust wave speed (radians/sec-ish)
    float fadeStart = 0.0f;// metres; blades shrink to the ground between these
    float fadeEnd = 0.0f;
    glm::vec3 fogColor = glm::vec3(0.62f, 0.71f, 0.85f);
    float fogDensity = 0.0f;

    GrassMaterial() : Material(MaterialProps{}) {
        renderState.cull = CullMode::None;// blades are two-sided
    }
};

// A terrain IS a PBR surface (albedo + normal + AO + per-layer roughness),
// lit by the same physically-based model — so it derives from PBRMaterial and
// adds the terrain-specific machinery (heightmap displacement, tessellation,
// splat-blended detail layers, aerial fog). The current terrain.frag still
// shades Lambertian-diffuse and ignores roughness/metallic, but those inherited
// fields are semantically correct and the natural home for a future PBR pass.
class TerrainMaterial : public PBRMaterial {
public:
    static constexpr int MAX_LAYERS = 4;

    // Displacement / normal source for the terrain shader; baked from the height
    // field by TerrainMeshComponent / TerrainStreamer.
    TextureHandle heightMap;
    float heightScale = 32.0f;
    float tessellationFactor = 16.0f;
    float worldSize = 1024.0f;// XZ extent; needed to derive normals from the heightmap
    // Fallback height-palette selection (0-5), used when no color/detail maps
    // are set. Matches the VoxelWorld palettes; 0 = legacy warm pink/gold.
    int paletteIndex = 0;

    TextureHandle splatMap;
    TerrainLayer layers[MAX_LAYERS];
    int layerCount = 0;

    // Aerial perspective: distant terrain fades toward fogColor with
    // 1-exp(-fogDensity*distance). The single biggest lever for perceived
    // world scale — without it a mountain 5km away reads like a hill 500m
    // away. 0 disables (default, keeps legacy terrains unchanged); ~0.00018
    // gives ~30% fade at 2km and ~80% at 10km.
    glm::vec3 fogColor = glm::vec3(0.62f, 0.71f, 0.85f);
    float fogDensity = 0.0f;

    TerrainMaterial() : PBRMaterial(MaterialProps{}) {
    }
    explicit TerrainMaterial(const MaterialProps& props) : PBRMaterial(props) {
        heightMap = props.heightMap;
    }

    void DrawImGui() override;
};

// Voxel-chunk surface. VoxelChunkPass samples one of 6 hard-coded palettes
// keyed by paletteIndex; the ownership contract is that VoxelWorldComponent
// creates one per world and pushes _world.paletteIndex into it each frame,
// so multi-world scenes can hold different palettes concurrently. No
// textures — voxel color comes entirely from the palette table.
class VoxelMaterial : public Material {
public:
    int paletteIndex = 4;// 0-5; 4 = VX Palette 5 (soft cool blue-grey, matches old default)

    VoxelMaterial() : Material(MaterialProps{}) {
        renderQueue = RenderQueue::Opaque;
    }

    // VoxelChunkPass ignores every field the base inspector touches (maps,
    // Phong colors, cull mode — the pass hardcodes back-face culling and
    // reads palette off VoxelWorld); overriding to a no-op keeps the empty
    // controls out of the UI. Palette lives on VoxelWorld and is edited from
    // VoxelWorldComponent, not here.
    void DrawImGui() override {
    }
};

// std::hash specialization so `unordered_map<RenderState, T>` works out of the box.
template<> struct std::hash<RenderState> {
    size_t operator()(const RenderState& s) const noexcept {
        return s.Hash();
    }
};

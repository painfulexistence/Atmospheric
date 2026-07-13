#pragma once
#include "batch_renderer_2d.hpp"
#include "buffer.hpp"
#include "globals.hpp"
#include <cstddef>
#include <functional>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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

struct MaterialProps {
    TextureHandle baseMap;
    TextureHandle normalMap;
    TextureHandle aoMap;
    TextureHandle roughnessMap;
    TextureHandle metallicMap;
    TextureHandle heightMap;
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);
    glm::vec3 specular = glm::vec3(.7, .7, .7);
    glm::vec3 ambient = glm::vec3(0, 0, 0);
    float shininess = .25;
    bool cullFaceEnabled = true;
    PrimitiveTopology primitiveType = PrimitiveTopology::Triangles;
    PolygonMode polygonMode = PolygonMode::Fill;
};

class Material {
public:
    TextureHandle baseMap;
    TextureHandle normalMap;
    TextureHandle aoMap;
    TextureHandle roughnessMap;
    TextureHandle metallicMap;
    TextureHandle heightMap;
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);
    glm::vec3 specular = glm::vec3(.7, .7, .7);
    glm::vec3 ambient = glm::vec3(0, 0, 0);
    float shininess = .25;

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

    // Owned material inspector. Base class draws the generic PBR/Phong fields
    // (maps, colors, shininess, cull mode); subclasses call the base and add
    // their own controls so per-type tunables (palette, wave strength, etc.)
    // live with the material instead of leaking into every component's
    // DrawImGui.
    virtual void DrawImGui();

    Material(const MaterialProps& props) {
        baseMap = props.baseMap;
        normalMap = props.normalMap;
        aoMap = props.aoMap;
        roughnessMap = props.roughnessMap;
        metallicMap = props.metallicMap;
        heightMap = props.heightMap;
        diffuse = props.diffuse;
        specular = props.specular;
        ambient = props.ambient;
        shininess = props.shininess;
        renderState.cull = props.cullFaceEnabled ? CullMode::Back : CullMode::None;
        renderState.topology = props.primitiveType;
        renderState.polygon = props.polygonMode;
    }
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
// (see vat.hpp). Carries the clip (non-owning — the VATComponent owns it) and
// the current normalized playhead the renderer feeds to vat.vert. Rendered by
// the standard opaque pass with the "vat" shader instead of "color"; all the
// usual surface/texture fields are inherited and still apply.
class VATMaterial : public Material {
public:
    // Non-owning; the owning VATComponent outlives the material's use in a draw.
    VATClip* clip = nullptr;
    float normalizedTime = 0.0f;// playhead in [0, 1], advanced by VATComponent

    VATMaterial() : Material(MaterialProps{}) {
    }
    explicit VATMaterial(const MaterialProps& props) : Material(props) {
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
//   heightMap (inherited) — 16-bit displacement map (baked by TerrainMeshComponent)
//   baseMap   (inherited) — full-terrain color/texture map, 0-1 UV
//   normalMap (inherited) — full-terrain normal map (overrides heightmap-derived normals)
//   aoMap     (inherited) — full-terrain ambient occlusion
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

// Draped river ribbons (MeshType::RIVER). Flow-animated water: normals + foam
// scroll along the ribbon's V axis (downstream), fresnel-blended deep/shallow
// water with a sun glint, alpha-blended over the terrain. Shares the terrain's
// aerial-perspective fog so distant rivers recede with the hills.
class RiverMaterial : public Material {
public:
    glm::vec3 shallowColor = glm::vec3(0.28f, 0.52f, 0.55f);// bank/edge water
    glm::vec3 deepColor = glm::vec3(0.05f, 0.16f, 0.24f);// channel centre
    float flowSpeed = 0.35f;// V-scroll speed of the ripple/foam (units/sec)
    float rippleStrength = 0.35f;// normal-perturbation amount
    float glint = 0.6f;// specular sun-glint intensity
    float alpha = 0.82f;// overall surface opacity
    glm::vec3 fogColor = glm::vec3(0.62f, 0.71f, 0.85f);
    float fogDensity = 0.0f;

    RiverMaterial() : Material(MaterialProps{}) {
        renderQueue = RenderQueue::Transparent;// alpha-blended over terrain
        renderState.cull = CullMode::None;// ribbon seen from either bank
    }
};

class TerrainMaterial : public Material {
public:
    static constexpr int MAX_LAYERS = 4;

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

    TerrainMaterial() : Material(MaterialProps{}) {
    }
    explicit TerrainMaterial(const MaterialProps& props) : Material(props) {
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

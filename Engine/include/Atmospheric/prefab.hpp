#pragma once
#include "animation_clip.hpp"
#include "vertex.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Unified prefab import layer
//
// ImportPrefab parses .map / .gltf / .usd into a Prefab — a pure-CPU, GPU-free
// entity subtree: mesh geometry, decoded images, material definitions, lights,
// convex colliders, and a node tree of transforms referencing them by index.
// It performs NO GL calls, so it is safe to run off the main thread (matching
// the engine's Phase-1 "pure parse" split).
//
// Turning a Prefab into live GameObjects + uploaded GPU resources is the
// separate, main-thread Application::Instantiate. The single-mesh loaders
// (LoadTBMap / LoadGLTF / LoadUSD) remain thin flatten-wrappers, so each format
// has exactly one parsing implementation.
//
// A Prefab is the engine's imported, read-only prefab (cf. Unity's Model
// Prefab): scene vs prefab is a role distinction, not a type one.
// ─────────────────────────────────────────────────────────────────────────────

// A decoded, tightly-packed 8-bit image (pixels.size() == width*height*channels).
struct PrefabImage {
    std::string name;// identifier (uri / prim path); used for texture caching
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<uint8_t> pixels;
};

// PBR-ish material description. Texture slots index Prefab::images (-1 = none);
// scalar factors apply when the slot is empty (Instantiate bakes them into 1x1
// textures, the same pattern the examples use).
struct PrefabMaterial {
    std::string name;
    glm::vec3 baseColor{ 1.0f };
    float metallic = 0.0f;
    float roughness = 1.0f;
    glm::vec3 emissive{ 0.0f };// approximated via Material::ambient at instantiate
    bool doubleSided = false;
    int baseColorImage = -1;
    int normalImage = -1;
    int metallicRoughnessImage = -1;// glTF packs metallic(B) + roughness(G)
    int occlusionImage = -1;
    int emissiveImage = -1;

    // ── Transmission / volume / IOR (glTF KHR_materials_transmission,
    //    KHR_materials_volume, KHR_materials_ior) ──────────────────────────────
    // Imported as data only; the surface shader does not yet consume these — a
    // transmission/refraction render pass is a separate concern. Defaults make a
    // fully opaque dielectric (transmission 0, thin-walled), so untouched
    // materials are unaffected.
    float transmissionFactor = 0.0f;// 0 = opaque; 1 = fully transmissive (glass)
    float ior = 1.5f;// index of refraction (glTF default 1.5)
    float thicknessFactor = 0.0f;// 0 = thin-walled (surface only); >0 = has a volume
    // Beer-Lambert absorption distance through the volume; +inf = no absorption
    // (the glTF default). Only meaningful when thicknessFactor > 0.
    float attenuationDistance = std::numeric_limits<float>::infinity();
    glm::vec3 attenuationColor{ 1.0f };// tint light picks up traversing the volume
    int transmissionImage = -1;// R channel scales transmissionFactor
    int thicknessImage = -1;// G channel scales thicknessFactor
};

// A punctual light attached to a node (from .map "light" entities or glTF
// KHR_lights_punctual). Position/direction come from the node transform.
struct PrefabLight {
    enum class Type { Point, Directional, Spot };
    Type type = Type::Point;
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
    float range = 0.0f;// 0 = unbounded
};

// A convex collider (e.g. one .map brush): the convex hull of `points`,
// in the owning node's local space.
struct PrefabCollider {
    std::vector<glm::vec3> points;
};

// One drawable unit: geometry plus material binding. `materialIndex` points
// into Prefab::materials (-1 = none); `material` is a name to resolve against
// already-registered engine materials (the .map texture-name path). When both
// are unset the engine default material is used.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::string material;// resolve-by-name (.map texture name)
    int materialIndex = -1;// index into Prefab::materials (gltf/usd)
    // .map classic/Valve220 UVs are authored in *texels*; Instantiate divides by
    // the real texture size once known (fallback 64, the classic Quake default).
    bool uvInTexels = false;
    bool visible = true;// false = collision-only (clip/trigger brushes)
};

// A node in the prefab hierarchy: a local transform, content references
// (indices into the Prefab's flat arrays), and children. For .map entities,
// classname/properties carry the full key/value block (point entities included,
// e.g. info_player_start), so gameplay code can query spawn data pre-instantiate.
// A named node-animation clip's tracks for the owning node: the node's local
// TRS keyframes from one glTF/USD animation (translation → Position, rotation →
// RotationQuat, scale → Scale). Instantiate turns these into an ActionTimeline
// on the node's GameObject. Skinned (skeletal) animation is separate.
struct PrefabNodeClip {
    std::string name;
    std::vector<ActionTrack> tracks;
};

struct PrefabNode {
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<int> meshes;
    std::vector<int> colliders;
    std::vector<int> lights;
    std::string classname;// .map entity classname ("" otherwise)
    std::unordered_map<std::string, std::string> properties;// .map key/values
    std::vector<PrefabNodeClip> animations;// per-clip local-TRS tracks for this node
    std::vector<PrefabNode> children;
};

struct Prefab {
    std::vector<MeshData> meshes;
    std::vector<PrefabMaterial> materials;
    std::vector<PrefabImage> images;
    std::vector<PrefabLight> lights;
    std::vector<PrefabCollider> colliders;
    PrefabNode root;
    bool ok = false;

    // Depth-first search for nodes by .map classname (e.g. "info_player_start").
    std::vector<const PrefabNode*> FindEntities(const std::string& classname) const;
};

// Split a MeshData whose vertex count exceeds the engine's 16-bit index ceiling
// into <=65535-vertex chunks (re-indexed per chunk, material fields copied).
// Returns {md} unchanged when it already fits.
std::vector<MeshData> SplitMeshData(const MeshData& md);

// Dispatch by file extension. `scale` currently only affects .map (Quake units
// are large; 1/32 maps one grid step to one engine unit).
// Returns Prefab{ ok = false } on failure or an unsupported/uncompiled format.
Prefab ImportPrefab(const std::string& path, float scale = 1.0f / 32.0f);

// ── Format-specific importers (exposed for direct use and testing) ───────────

// TrenchBroom / Quake ".map" brush format: classic, Valve 220, and Q3 brush
// primitives (brushDef) faces plus patchDef2 Bezier patches. Every brush entity
// becomes per-texture mesh batches + per-brush convex colliders; all entity
// key/values (point entities included) are preserved on nodes. Quake Z-up is
// converted to the engine's Y-up and scaled by `scale`. Reads the file through
// the engine FileSystem.
Prefab ImportMapPrefab(const std::string& path, float scale = 1.0f / 32.0f);

// Same, but parsing already-loaded ".map" text — the pure, I/O-free core (also
// used by tests). `name` becomes the root node's name.
Prefab ImportMapPrefabFromText(const std::string& text, const std::string& name, float scale = 1.0f / 32.0f);

// glTF / GLB via tinygltf: node hierarchy, per-primitive meshes, PBR materials
// with decoded textures (embedded and external), KHR_lights_punctual lights,
// KHR_texture_transform (baked into UVs), KHR_materials_emissive_strength.
// Implemented in prefab_gltf.cpp.
Prefab ImportGLTFPrefab(const std::string& path);

// USD (.usd/.usda/.usdc/.usdz) via TinyUSDZ + Tydra: the USD Xform node tree,
// per-GeomMesh meshes, UsdPreviewSurface materials with decoded textures, and
// stage upAxis/metersPerUnit applied at the root. Returns Prefab{ ok = false }
// unless built with AE_USE_TINYUSDZ. Implemented in prefab_usd.cpp.
Prefab ImportUSDPrefab(const std::string& path);

#pragma once
#include "vertex.hpp"
#include <glm/mat4x4.hpp>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Unified model import ("prefab") layer
//
// ImportModel parses .map / .gltf / .usd into a ModelData — a pure-CPU, GPU-free
// description of a model: a flat list of mesh geometry plus a node tree of
// transforms that reference those meshes. It performs NO GL calls, so it is safe
// to run off the main thread (matching the engine's Phase-1 "pure parse" split).
//
// Turning a ModelData into live GameObjects + uploaded meshes is a separate,
// main-thread step (Application::InstantiateModel). The three legacy loaders
// (LoadTBMap / LoadGLTF / LoadUSD) are thin wrappers that import then flatten to
// a single mesh, so each format has exactly one parsing implementation.
//
// This is the "prefab" the scene format wants: reusable, instanceable, and
// hierarchy-preserving. Scene vs prefab is a role distinction, not a type one —
// a ModelData is a prefab; a scene is the root you load and run.
// ─────────────────────────────────────────────────────────────────────────────

// One drawable unit: geometry plus an optional engine material name (empty =
// use the engine default). 16-bit indices, local to this mesh (0-based).
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::string material;
};

// A node in the import hierarchy: a local transform (relative to its parent),
// zero or more meshes (indices into ModelData::meshes), and children.
struct ModelNode {
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<int> meshes;
    std::vector<ModelNode> children;
};

struct ModelData {
    std::vector<MeshData> meshes;
    ModelNode root;
    bool ok = false;
};

// Dispatch by file extension. `scale` currently only affects .map (Quake units
// are large; 1/32 maps one grid step to one engine unit). Returns
// ModelData{ ok = false } on failure or an unsupported/uncompiled format.
//
// glTF and USD are routed to ImportGLTFModel / ImportUSDModel (declared once
// those importers land — see model-loading.md). For now only .map is wired.
ModelData ImportModel(const std::string& path, float scale = 1.0f / 32.0f);

// ── Format-specific importers (exposed for direct use and testing) ───────────

// TrenchBroom / Quake ".map" brush format. Every brush entity becomes one mesh
// under a child node named after its classname; Quake Z-up is converted to the
// engine's Y-up and scaled by `scale`. Reads the file through the engine
// FileSystem (paths resolve against the executable dir, like every other asset).
ModelData ImportMapModel(const std::string& path, float scale = 1.0f / 32.0f);

// Same, but parsing already-loaded ".map" text — the pure, I/O-free core (also
// used by tests). `name` becomes the root node's name.
ModelData ImportMapModelFromText(const std::string& text, const std::string& name, float scale = 1.0f / 32.0f);

// glTF / GLB via tinygltf: the scene node hierarchy becomes a ModelNode tree and
// each primitive becomes a MeshData tagged with the glTF material name. Geometry
// and hierarchy only — glTF textures are not built here (use LoadGLTF for the
// single-mesh, textured path). Implemented in asset_manager.cpp.
ModelData ImportGLTFModel(const std::string& path);

// USD (.usd/.usda/.usdc/.usdz) via TinyUSDZ + Tydra: one MeshData per RenderMesh.
// Returns ModelData{ ok = false } unless built with AE_USE_TINYUSDZ.
// Implemented in asset_manager.cpp.
ModelData ImportUSDModel(const std::string& path);

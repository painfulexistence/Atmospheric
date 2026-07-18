// Format-agnostic prefab layer: the extension dispatch (ImportPrefab) plus the
// shared IR helpers used by every importer. Each concrete format lives in its
// own translation unit — prefab_map.cpp (Quake .map), prefab_gltf.cpp (glTF/GLB),
// prefab_usd.cpp (USD via TinyUSDZ). This file performs no format parsing itself.
#include "prefab.hpp"
#include <cstring>
#include <unordered_map>

Prefab ImportPrefab(const std::string& path, float scale) {
    auto endsWith = [&](const char* ext) {
        return path.size() >= std::strlen(ext)
               && path.compare(path.size() - std::strlen(ext), std::strlen(ext), ext) == 0;
    };
    if (endsWith(".map")) return ImportMapPrefab(path, scale);
    if (endsWith(".gltf") || endsWith(".glb")) return ImportGLTFPrefab(path);
    if (endsWith(".usd") || endsWith(".usda") || endsWith(".usdc") || endsWith(".usdz")) return ImportUSDPrefab(path);
    return Prefab{};
}

// ── Shared helpers ────────────────────────────────────────────────────────────

std::vector<const PrefabNode*> Prefab::FindEntities(const std::string& classname) const {
    std::vector<const PrefabNode*> out;
    // Iterative DFS to avoid recursion-lambda plumbing.
    std::vector<const PrefabNode*> stack{ &root };
    while (!stack.empty()) {
        const PrefabNode* n = stack.back();
        stack.pop_back();
        if (n->classname == classname) out.push_back(n);
        for (const auto& c : n->children)
            stack.push_back(&c);
    }
    return out;
}

// Re-index a too-large MeshData into <=65535-vertex chunks. Walks triangles in
// order, mapping source vertices into the current chunk until it would
// overflow, then starts a new one (vertices shared across chunk boundaries are
// duplicated — correctness over optimality).
std::vector<MeshData> SplitMeshData(const MeshData& md) {
    if (md.vertices.size() <= 65535) return { md };

    std::vector<MeshData> out;
    MeshData cur;
    cur.material = md.material;
    cur.materialIndex = md.materialIndex;
    cur.uvInTexels = md.uvInTexels;
    cur.visible = md.visible;
    std::unordered_map<uint32_t, uint16_t> remap;

    auto flush = [&]() {
        if (!cur.vertices.empty()) out.push_back(std::move(cur));
        cur = MeshData{};
        cur.material = md.material;
        cur.materialIndex = md.materialIndex;
        cur.uvInTexels = md.uvInTexels;
        cur.visible = md.visible;
        remap.clear();
    };

    for (size_t t = 0; t + 2 < md.indices.size(); t += 3) {
        // A triangle may add up to 3 new vertices.
        if (cur.vertices.size() + 3 > 65535) flush();
        for (int k = 0; k < 3; ++k) {
            const uint32_t src = md.indices[t + k];
            auto it = remap.find(src);
            uint16_t idx;
            if (it != remap.end()) {
                idx = it->second;
            } else {
                idx = static_cast<uint16_t>(cur.vertices.size());
                cur.vertices.push_back(md.vertices[src]);
                remap[src] = idx;
            }
            cur.indices.push_back(idx);
        }
    }
    flush();
    return out;
}

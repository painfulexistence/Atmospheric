// USD (.usd/.usda/.usdc/.usdz) → Prefab importer via TinyUSDZ + Tydra.
// Preserves the USD node (Xform) hierarchy, extracts UsdPreviewSurface
// materials with decoded textures, and applies stage upAxis / metersPerUnit at
// the prefab root. Pure CPU; compiled to a warning stub without AE_USE_TINYUSDZ.
#include "console_subsystem.hpp"
#include "file_system.hpp"
#include "prefab.hpp"

#include "fmt/core.h"
#include <algorithm>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

#ifndef AE_USE_TINYUSDZ

Prefab ImportUSDPrefab(const std::string& path) {
    ConsoleSubsystem::Get()->Warn(
        fmt::format("ImportUSDPrefab '{}': USD support not compiled in (build with -DAE_USE_TINYUSDZ=ON)", path)
    );
    return Prefab{};
}

#else

#include <tinyusdz.hh>
#include <tydra/render-data.hh>

namespace {

    // tinyusdz matrices are row-major with USD's row-vector (pre-multiply)
    // convention; copying rows into glm columns yields the equivalent
    // column-vector matrix (row i = image of basis i = glm column i).
    glm::mat4 ToGlm(const tinyusdz::value::matrix4d& m) {
        glm::mat4 g(1.0f);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                g[i][j] = static_cast<float>(m.m[i][j]);
        return g;
    }

    // Extract a Tydra RenderMesh into <=65535-vertex chunks, expanding to a
    // non-indexed triangle list (one vertex per face-vertex — copes with both
    // 'vertex' and 'facevarying' attribute variability).
    void ExtractMesh(
        const tinyusdz::tydra::RenderMesh& rmesh,
        int materialIndex,
        const std::string& materialName,
        std::vector<MeshData>& outMeshes,
        std::vector<int>& outIndices
    ) {
        const std::vector<uint32_t>& idx = rmesh.faceVertexIndices();// triangulated
        const size_t pointCount = rmesh.points.size();
        if (idx.empty() || pointCount == 0) return;

        auto normalAt = [&](size_t k, uint32_t pointIndex) -> glm::vec3 {
            const auto& a = rmesh.normals;
            const size_t vc = a.vertex_count();
            if (vc == 0 || a.format_size() < sizeof(float) * 3) return glm::vec3(0, 1, 0);
            const auto* f = reinterpret_cast<const float*>(a.buffer());
            size_t at = (vc == pointCount) ? pointIndex : k;
            if (at >= vc) return glm::vec3(0, 1, 0);
            return glm::vec3(f[at * 3 + 0], f[at * 3 + 1], f[at * 3 + 2]);
        };
        auto uvAt = [&](size_t k, uint32_t pointIndex) -> glm::vec2 {
            auto it = rmesh.texcoords.find(0);
            if (it == rmesh.texcoords.end()) return glm::vec2(0);
            const auto& a = it->second;
            const size_t vc = a.vertex_count();
            if (vc == 0 || a.format_size() < sizeof(float) * 2) return glm::vec2(0);
            const auto* f = reinterpret_cast<const float*>(a.buffer());
            size_t at = (vc == pointCount) ? pointIndex : k;
            if (at >= vc) return glm::vec2(0);
            return glm::vec2(f[at * 2 + 0], f[at * 2 + 1]);
        };

        // 65535 is a multiple of 3, so chunk boundaries never split a triangle.
        for (size_t start = 0; start < idx.size(); start += 65535) {
            const size_t take = std::min<size_t>(idx.size() - start, 65535);
            if (take < 3) break;
            MeshData md;
            md.materialIndex = materialIndex;
            md.material = materialName;
            md.vertices.reserve(take);
            for (size_t k = 0; k < take; ++k) {
                const uint32_t pointIndex = idx[start + k];
                glm::vec3 pos(0.0f);
                if (pointIndex < pointCount) {
                    // Tydra points are std::array<float,3>-like, not .x/.y/.z.
                    const auto& p = rmesh.points[pointIndex];
                    pos = glm::vec3(p[0], p[1], p[2]);
                }
                Vertex vert;
                vert.position = pos;
                vert.normal = normalAt(start + k, pointIndex);
                vert.uv = uvAt(start + k, pointIndex);
                vert.tangent = glm::vec3(1, 0, 0);
                vert.bitangent = glm::cross(vert.normal, vert.tangent);
                md.vertices.push_back(vert);
            }
            for (uint16_t k = 0; k + 2 < static_cast<uint32_t>(md.vertices.size()); k += 3) {
                md.indices.push_back(k);
                md.indices.push_back(static_cast<uint16_t>(k + 1));
                md.indices.push_back(static_cast<uint16_t>(k + 2));
            }
            if (md.vertices.empty()) continue;
            outIndices.push_back(static_cast<int>(outMeshes.size()));
            outMeshes.push_back(std::move(md));
        }
    }

}// namespace

Prefab ImportUSDPrefab(const std::string& path) {
    // Resolve against the executable dir (like .map) so loading is independent of
    // the working directory, and so it reads from Emscripten's MEMFS. tinyusdz
    // then resolves external references relative to this path. Fall back to the
    // raw path so the loader still reports its own open error.
    const std::string realPath = FileSystem::Get().ResolvePath(path).value_or(path);

    tinyusdz::Stage stage;
    std::string warn, err;
    if (!tinyusdz::LoadUSDFromFile(realPath, &stage, &warn, &err)) {
        if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));
        if (!err.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}' error: {}", path, err));
        return Prefab{};
    }
    if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));

    tinyusdz::tydra::RenderSceneConverter converter;
    tinyusdz::tydra::RenderSceneConverterEnv env(stage);
    const size_t slash = realPath.find_last_of("/\\");
    if (slash != std::string::npos) env.set_search_paths({ realPath.substr(0, slash) });
    env.scene_config.load_texture_assets = true;// decode textures for materials
    // Keep 8-bit texels 8-bit — Tydra otherwise converts textures to fp32
    // (observed: a 768x768 jpg became a 9.4MB float buffer).
    env.material_config.preserve_texel_bitdepth = true;

    tinyusdz::tydra::RenderScene rscene;
    if (!converter.ConvertToRenderScene(env, &rscene)) {
        ConsoleSubsystem::Get()->Warn(
            fmt::format("ImportUSDPrefab '{}' convert error: {}", path, converter.GetError())
        );
        return Prefab{};
    }

    Prefab out;

    // ── Images (Tydra decodes into rscene.buffers via its builtin loader) ────
    // Map rscene image index -> out.images index (-1 when undecodable).
    std::vector<int> imageRemap(rscene.images.size(), -1);
    for (size_t i = 0; i < rscene.images.size(); ++i) {
        const auto& ti = rscene.images[i];
        if (ti.buffer_id < 0 || ti.buffer_id >= static_cast<int64_t>(rscene.buffers.size())) continue;
        const auto& buf = rscene.buffers[ti.buffer_id];
        if (!ti.decoded || ti.width <= 0 || ti.height <= 0 || ti.channels <= 0) continue;
        const size_t texels = static_cast<size_t>(ti.width) * ti.height * ti.channels;
        PrefabImage pi;
        pi.name = ti.asset_identifier.empty() ? fmt::format("{}#image{}", path, i) : ti.asset_identifier;
        pi.width = ti.width;
        pi.height = ti.height;
        pi.channels = ti.channels;
        if (buf.componentType == tinyusdz::tydra::ComponentType::UInt8 && buf.data.size() >= texels) {
            pi.pixels.assign(buf.data.begin(), buf.data.begin() + texels);
        } else if (buf.componentType == tinyusdz::tydra::ComponentType::Float
                   && buf.data.size() >= texels * sizeof(float)) {
            // Fallback for Tydra's fp32 conversion path: quantize back to 8-bit.
            const float* f = reinterpret_cast<const float*>(buf.data.data());
            pi.pixels.resize(texels);
            for (size_t t = 0; t < texels; ++t)
                pi.pixels[t] = static_cast<uint8_t>(std::clamp(f[t], 0.0f, 1.0f) * 255.0f + 0.5f);
        } else {
            continue;// unsupported texel layout
        }
        imageRemap[i] = static_cast<int>(out.images.size());
        out.images.push_back(std::move(pi));
    }

    // ── Materials (UsdPreviewSurface → PrefabMaterial) ───────────────────────
    auto imageOfTexture = [&](int64_t texId) -> int {
        if (texId < 0 || texId >= static_cast<int64_t>(rscene.textures.size())) return -1;
        const int64_t img = rscene.textures[texId].texture_image_id;
        if (img < 0 || img >= static_cast<int64_t>(imageRemap.size())) return -1;
        return imageRemap[img];
    };
    for (const auto& rm : rscene.materials) {
        const auto& s = rm.surfaceShader;
        PrefabMaterial pm;
        pm.name = rm.name.empty() ? rm.abs_path : rm.name;
        pm.baseColor = { s.diffuseColor.value[0], s.diffuseColor.value[1], s.diffuseColor.value[2] };
        pm.metallic = s.metallic.value;
        pm.roughness = s.roughness.value;
        pm.emissive = { s.emissiveColor.value[0], s.emissiveColor.value[1], s.emissiveColor.value[2] };
        if (s.diffuseColor.is_texture()) pm.baseColorImage = imageOfTexture(s.diffuseColor.texture_id);
        if (s.normal.is_texture()) pm.normalImage = imageOfTexture(s.normal.texture_id);
        if (s.roughness.is_texture()) pm.metallicRoughnessImage = imageOfTexture(s.roughness.texture_id);
        if (s.occlusion.is_texture()) pm.occlusionImage = imageOfTexture(s.occlusion.texture_id);
        if (s.emissiveColor.is_texture()) pm.emissiveImage = imageOfTexture(s.emissiveColor.texture_id);
        out.materials.push_back(std::move(pm));
    }

    // ── Meshes (chunked; keep a per-RenderMesh list for node references) ─────
    std::vector<std::vector<int>> meshChunks(rscene.meshes.size());
    for (size_t i = 0; i < rscene.meshes.size(); ++i) {
        const auto& rmesh = rscene.meshes[i];
        // Tydra records the bound material per mesh via materialMap; RenderMesh
        // carries material_id in recent versions — resolve defensively.
        int matIndex = -1;
        std::string matName;
        if (rmesh.material_id >= 0 && rmesh.material_id < static_cast<int>(out.materials.size())) {
            matIndex = rmesh.material_id;
            matName = out.materials[matIndex].name;
        }
        ExtractMesh(rmesh, matIndex, matName, out.meshes, meshChunks[i]);
    }

    // ── Node hierarchy (mesh points are local; nodes carry the transforms) ───
    std::function<PrefabNode(const tinyusdz::tydra::Node&)> buildNode =
        [&](const tinyusdz::tydra::Node& n) -> PrefabNode {
        PrefabNode node;
        node.name = n.prim_name.empty() ? n.abs_path : n.prim_name;
        node.transform = ToGlm(n.local_matrix);
        if (n.nodeType == tinyusdz::tydra::NodeType::Mesh && n.id >= 0
            && n.id < static_cast<int32_t>(meshChunks.size()))
            node.meshes = meshChunks[n.id];
        for (const auto& c : n.children)
            node.children.push_back(buildNode(c));
        return node;
    };

    out.root.name = (slash == std::string::npos) ? realPath : realPath.substr(slash + 1);
    if (rscene.default_root_node < rscene.nodes.size()) {
        out.root.children.push_back(buildNode(rscene.nodes[rscene.default_root_node]));
    } else {
        for (auto& chunks : meshChunks)
            for (int c : chunks)
                out.root.meshes.push_back(c);
    }

    // ── Stage metadata: upAxis + metersPerUnit at the root ───────────────────
    // Read from the Stage itself — Tydra's RenderScene.meta is not populated
    // from the stage metas (observed: file says Z/0.01, rscene.meta says Y/1).
    glm::mat4 rootXf(1.0f);
    const float mpu = static_cast<float>(stage.metas().metersPerUnit.get_value());
    if (mpu > 0.0f && std::abs(mpu - 1.0f) > 1e-6f) rootXf = glm::scale(rootXf, glm::vec3(mpu));
    const tinyusdz::Axis up = stage.metas().upAxis.get_value();
    if (up == tinyusdz::Axis::Z)
        rootXf = glm::rotate(rootXf, glm::radians(-90.0f), glm::vec3(1, 0, 0));// Z-up → Y-up
    else if (up == tinyusdz::Axis::X)
        rootXf = glm::rotate(rootXf, glm::radians(-90.0f), glm::vec3(0, 0, 1));// X-up → Y-up
    out.root.transform = rootXf;

    out.ok = !out.meshes.empty();
    return out;
}

#endif// AE_USE_TINYUSDZ

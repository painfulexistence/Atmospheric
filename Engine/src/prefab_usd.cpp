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

#include <asset-resolution.hh>
#include <composition.hh>
#include <cstring>
#include <map>
#include <tydra/render-data.hh>

namespace {

    // ── FileSystem-backed asset resolver ─────────────────────────────────────
    // Referenced/payloaded sub-USDs and material textures are loaded through the
    // engine FileSystem (not tinyusdz's own fopen), so they resolve identically
    // on native (disk) and web (prefetched cache) — the same reason the root
    // file is read via ReadSync. A transient byte cache avoids reading each
    // sub-asset twice (size_fun then read_fun).
    struct FsResolverCtx {
        std::map<std::string, FileSystem::Bytes> cache;// resolved key -> bytes
    };

    std::string NormalizeRel(std::string p) {
        if (p.rfind("./", 0) == 0) p = p.substr(2);
        return p;
    }

    // tinyusdz passes the referencing layer's directory in `searchPaths` when it
    // descends into nested references, so joining the asset path against each
    // search dir resolves deeply-nested sub-files (e.g. Kitchen_set → assets/ →
    // assets/props/).
    int FsResolve(
        const char* asset,
        const std::vector<std::string>& searchPaths,
        std::string* resolved,
        std::string* err,
        void* /*ud*/
    ) {
        const std::string a = NormalizeRel(asset);
        for (const auto& sp : searchPaths) {
            const std::string base = NormalizeRel(sp);
            const std::string cand = (base.empty() || base == ".") ? a : base + "/" + a;
            if (FileSystem::Get().Exists(cand)) {
                *resolved = cand;
                return 0;
            }
        }
        if (FileSystem::Get().Exists(a)) {// last resort: relative to the asset root
            *resolved = a;
            return 0;
        }
        if (err) *err = fmt::format("asset not found: {}", asset);
        return -1;
    }

    FileSystem::Bytes* FsFetch(FsResolverCtx* ctx, const char* resolved) {
        auto it = ctx->cache.find(resolved);
        if (it == ctx->cache.end()) {
            FileSystem::Bytes b = FileSystem::Get().ReadSync(resolved);
            if (b.empty()) return nullptr;
            it = ctx->cache.emplace(resolved, std::move(b)).first;
        }
        return &it->second;
    }

    int FsSize(const char* resolved, uint64_t* nbytes, std::string* err, void* ud) {
        FileSystem::Bytes* b = FsFetch(static_cast<FsResolverCtx*>(ud), resolved);
        if (!b) {
            if (err) *err = fmt::format("read failed: {}", resolved);
            return -1;
        }
        *nbytes = b->size();
        return 0;
    }

    int FsRead(
        const char* resolved,
        uint64_t req,
        uint8_t* out,
        uint64_t* nbytes,
        std::string* err,
        void* ud
    ) {
        FileSystem::Bytes* b = FsFetch(static_cast<FsResolverCtx*>(ud), resolved);
        if (!b) {
            if (err) *err = fmt::format("read failed: {}", resolved);
            return -1;
        }
        const uint64_t n = std::min<uint64_t>(req, b->size());
        std::memcpy(out, b->data(), n);
        *nbytes = n;
        return 0;
    }

    tinyusdz::AssetResolutionHandler MakeFsHandler(FsResolverCtx* ctx) {
        tinyusdz::AssetResolutionHandler h;
        h.resolve_fun = &FsResolve;
        h.size_fun = &FsSize;
        h.read_fun = &FsRead;
        h.write_fun = nullptr;
        h.userdata = ctx;
        return h;
    }

    // Load a USDA/USDC root as a Layer and flatten LIVRPS composition arcs
    // (subLayers, references, payloads, inherits, variants) into a single Stage.
    // tinyusdz does NOT compose by default — a plain LoadUSDFromMemory of a file
    // built from references/payloads (e.g. Pixar's Kitchen_set) yields zero
    // geometry because the referenced Prims are never pulled in. `baseDir` is the
    // virtual directory of the root file; sub-assets are read via `ctx`.
    bool ComposeStage(
        const FileSystem::Bytes& bytes,
        const std::string& virtualPath,
        const std::string& baseDir,
        FsResolverCtx* ctx,
        tinyusdz::Stage* outStage,
        std::string* warn,
        std::string* err
    ) {
        tinyusdz::Layer root;
        if (!tinyusdz::LoadLayerFromMemory(bytes.data(), bytes.size(), virtualPath, &root, warn, err))
            return false;

        tinyusdz::AssetResolutionResolver resolver;
        resolver.register_wildcard_asset_resolution_handler(MakeFsHandler(ctx));
        const std::string cwd = baseDir.empty() ? "." : baseDir;
        resolver.set_search_paths({ cwd });
        resolver.set_current_working_path(cwd);

        tinyusdz::Layer src = root;
        {
            tinyusdz::Layer composited;
            if (tinyusdz::CompositeSublayers(resolver, src, &composited, warn, err))
                src = std::move(composited);
        }
        // References/payloads can nest (a referenced layer references more); loop
        // until no arc remains unresolved (bounded to avoid a pathological cycle).
        constexpr int kMaxIterations = 128;
        for (int i = 0; i < kMaxIterations; ++i) {
            bool unresolved = false;
            if (src.check_unresolved_references()) {
                unresolved = true;
                tinyusdz::Layer composited;
                if (!tinyusdz::CompositeReferences(resolver, src, &composited, warn, err)) return false;
                src = std::move(composited);
            }
            if (src.check_unresolved_payload()) {
                unresolved = true;
                tinyusdz::Layer composited;
                if (!tinyusdz::CompositePayload(resolver, src, &composited, warn, err)) return false;
                src = std::move(composited);
            }
            if (src.check_unresolved_inherits()) {
                unresolved = true;
                tinyusdz::Layer composited;
                if (tinyusdz::CompositeInherits(src, &composited, warn, err)) src = std::move(composited);
            }
            if (src.check_unresolved_variant()) {
                unresolved = true;
                tinyusdz::Layer composited;
                if (tinyusdz::CompositeVariant(src, &composited, warn, err)) src = std::move(composited);
            }
            if (!unresolved) break;
        }

        // Preserve stage metas (upAxis / metersPerUnit) — LayerToStage keeps what
        // we seed here, and the importer reads them below for the root transform.
        outStage->metas() = root.metas();
        return tinyusdz::LayerToStage(src, outStage, warn, err);
    }

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
    // Read the bytes through FileSystem, not tinyusdz's own fopen(): on web the
    // file is fetched into the FileSystem cache (keyed by this relative path) and
    // never touches a real filesystem, so LoadUSDFromFile()'s fopen() fails with
    // "File open error". ReadSync returns the cached bytes on web and reads disk
    // on native; realPath is passed as the base dir for external-reference
    // resolution (self-contained assets ignore it).
    const std::string realPath = FileSystem::Get().ResolvePath(path).value_or(path);
    FileSystem::Bytes bytes = FileSystem::Get().ReadSync(path);
    if (bytes.empty()) {
        ConsoleSubsystem::Get()->Warn(
            fmt::format("ImportUSDPrefab '{}': file not found (native) or not prefetched (web)", path)
        );
        return Prefab{};
    }

    // Virtual (relative) directory of the root file — the key space the
    // FileSystem resolver works in (NOT the absolute realPath), so referenced
    // sub-files and textures resolve on web (cache) and native (disk) alike.
    std::string baseDir;
    {
        const size_t vslash = path.find_last_of("/\\");
        if (vslash != std::string::npos) baseDir = path.substr(0, vslash);
    }

    // USDZ is a self-contained zip archive; its internal assets are resolved by
    // the USDZ loader itself, so the memory loader is the right path there.
    // Everything else (usda/usdc) goes through composition so files built from
    // references/payloads (e.g. Kitchen_set) actually pull in their geometry.
    auto endsWith = [](const std::string& s, const char* suf) {
        const size_t n = std::strlen(suf);
        return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
    };
    const bool isUsdz = endsWith(path, ".usdz") || endsWith(realPath, ".usdz");

    tinyusdz::Stage stage;
    std::string warn, err;
    FsResolverCtx fsctx;
    const bool loaded =
        isUsdz ? tinyusdz::LoadUSDFromMemory(bytes.data(), bytes.size(), realPath, &stage, &warn, &err)
               : ComposeStage(bytes, path, baseDir, &fsctx, &stage, &warn, &err);
    if (!loaded) {
        if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));
        if (!err.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}' error: {}", path, err));
        return Prefab{};
    }
    if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));

    tinyusdz::tydra::RenderSceneConverter converter;
    tinyusdz::tydra::RenderSceneConverterEnv env(stage);
    if (isUsdz) {
        // USDZ textures are embedded in the archive and resolved by tinyusdz's
        // own asset map; the absolute base dir is the search root as before.
        const size_t s = realPath.find_last_of("/\\");
        if (s != std::string::npos) env.set_search_paths({ realPath.substr(0, s) });
    } else {
        // Resolve material textures through the same FileSystem-backed handler.
        // Search the root dir plus every sub-asset directory pulled in during
        // composition (fsctx keys), so textures beside deeply-nested sub-USDs
        // (Kitchen_set scatters them across assets/*/) are found.
        env.asset_resolver.register_wildcard_asset_resolution_handler(MakeFsHandler(&fsctx));
        std::vector<std::string> searchPaths;
        if (!baseDir.empty()) searchPaths.push_back(baseDir);
        for (const auto& [key, _] : fsctx.cache) {
            const size_t s = key.find_last_of("/\\");
            if (s != std::string::npos) searchPaths.push_back(key.substr(0, s));
        }
        std::sort(searchPaths.begin(), searchPaths.end());
        searchPaths.erase(std::unique(searchPaths.begin(), searchPaths.end()), searchPaths.end());
        if (searchPaths.empty()) searchPaths.push_back(".");
        env.set_search_paths(searchPaths);
    }
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

    const size_t nameSlash = realPath.find_last_of("/\\");
    out.root.name = (nameSlash == std::string::npos) ? realPath : realPath.substr(nameSlash + 1);
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
    ConsoleSubsystem::Get()->Info(
        fmt::format(
            "ImportUSDPrefab '{}': {} mesh(es), {} material(s), {} image(s)",
            path,
            out.meshes.size(),
            out.materials.size(),
            out.images.size()
        )
    );
    return out;
}

#endif// AE_USE_TINYUSDZ

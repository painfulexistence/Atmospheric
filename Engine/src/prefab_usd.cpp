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
#include <optional>
#include <cstdlib>
#include <set>
#include <tuple>
#include <tydra/render-data.hh>
#include <tydra/scene-access.hh>
#include <usda-writer.hh>

namespace {

    // ── FileSystem-backed asset resolver ─────────────────────────────────────
    // Referenced/payloaded sub-USDs and material textures are loaded through the
    // engine FileSystem (not tinyusdz's own fopen), so they resolve identically
    // on native (disk) and web (prefetched cache) — the same reason the root
    // file is read via ReadSync. A transient byte cache avoids reading each
    // sub-asset twice (size_fun then read_fun).
    struct FsResolverCtx {
        std::map<std::string, FileSystem::Bytes> cache;// resolved key -> bytes
        // The resolver whose handler this ctx backs. tinyusdz sets the resolver's
        // current_working_path() to the referencing layer's directory before each
        // resolve() but does NOT pass it to the handler callback — so we read it
        // off the resolver here. Without it, a reference like
        // `./assets/Cheerio/Cheerio.usd` (relative to the layer's dir, not to any
        // search path) fails to resolve and its sub-layer never loads.
        const tinyusdz::AssetResolutionResolver* resolver = nullptr;
    };

    // Resolve an asset against the current working path first (the referencing
    // layer's directory), then any search paths tinyusdz supplies, then the asset
    // root. The cwp branch is what makes deeply-nested Kitchen_set-style
    // references (`./assets/<Prop>/<Prop>.usd`) resolve.
    int FsResolve(
        const char* asset,
        const std::vector<std::string>& searchPaths,
        std::string* resolved,
        std::string* err,
        void* ud
    ) {
        const auto* ctx = static_cast<const FsResolverCtx*>(ud);
        std::vector<std::string> dirs;
        if (ctx && ctx->resolver) {
            const std::string& cwp = ctx->resolver->current_working_path();
            if (!cwp.empty()) dirs.push_back(cwp);
        }
        dirs.insert(dirs.end(), searchPaths.begin(), searchPaths.end());
        for (const auto& d : dirs) {
            const std::string cand = FileSystem::JoinPath(d, asset);
            if (FileSystem::Get().Exists(cand)) {
                *resolved = cand;
                return 0;
            }
        }
        const std::string bare = FileSystem::JoinPath("", asset);// relative to the asset root
        if (FileSystem::Get().Exists(bare)) {
            *resolved = bare;
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

    int FsRead(const char* resolved, uint64_t req, uint8_t* out, uint64_t* nbytes, std::string* err, void* ud) {
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

    // Resolve an asset path the way tinyusdz's LoadAsset does — against the
    // PrimSpec's own asset-resolution state first (set as composition descends
    // into sub-layers), then the root base dir — and verify via FileSystem.
    std::optional<std::string>
        PeekResolve(const std::string& assetPath, const tinyusdz::PrimSpec& ps, const std::string& baseDir) {
        std::vector<std::string> dirs;
        const std::string& cwp = ps.get_current_working_path();
        if (!cwp.empty()) dirs.push_back(cwp);
        for (const auto& s : ps.get_asset_search_paths())
            dirs.push_back(s);
        if (!baseDir.empty()) dirs.push_back(baseDir);
        dirs.push_back(".");
        for (const auto& d : dirs) {
            const std::string cand = FileSystem::JoinPath(d, assetPath);
            if (FileSystem::Get().Exists(cand)) return cand;
        }
        const std::string bare = FileSystem::JoinPath("", assetPath);
        if (FileSystem::Get().Exists(bare)) return bare;
        return std::nullopt;
    }

    // Read the effective root prim of a referenced/payloaded layer (defaultPrim,
    // else the first prim) — the prim the arc implicitly targets. Cached by path.
    bool PeekDefaultPrim(
        const std::string& resolved, tinyusdz::Path* out, std::map<std::string, tinyusdz::Path>& cache
    ) {
        auto it = cache.find(resolved);
        if (it != cache.end()) {
            *out = it->second;
            return it->second.is_valid();
        }
        tinyusdz::Path result;// invalid by default
        FileSystem::Bytes b = FileSystem::Get().ReadSync(resolved);
        if (!b.empty()) {
            tinyusdz::Layer layer;
            std::string w, e;
            if (tinyusdz::LoadLayerFromMemory(b.data(), b.size(), resolved, &layer, &w, &e)
                && !layer.primspecs().empty()) {
                const std::string name = layer.metas().defaultPrim.valid() ? layer.metas().defaultPrim.str()
                                                                           : layer.primspecs().begin()->first;
                result = tinyusdz::Path("/" + name, "");
            }
        }
        cache[resolved] = result;
        *out = result;
        return result.is_valid();
    }

    // Fill in the implicit prim_path (the referenced layer's defaultPrim) on every
    // reference/payload arc that omits one. tinyusdz translates a referenced
    // layer's internal target/connection paths across the arc via
    // ReplaceRootPrimPathRec(srcPrefix = arc.prim_path, ...) — but when the arc
    // relies on defaultPrim (the common `@file.usd@` form, no `</Prim>`),
    // arc.prim_path is empty, so the translation no-ops and bindings such as
    // `material:binding = </Prop/Looks/Mat>` dangle after flattening (the mesh
    // survives, its material is dropped). Supplying the defaultPrim as the
    // prefix — exactly what the arc means — makes the existing translation fire,
    // for every path-valued field (material/skel bindings, collections,
    // connections), not just one kind.
    void FillArcPrimPaths(
        tinyusdz::PrimSpec& ps, const std::string& baseDir, std::map<std::string, tinyusdz::Path>& cache
    ) {
        auto fix = [&](auto& arcsPair) {
            // The pinned compositor rejects `add`-qualified arcs outright
            // (Kitchen_set.usd uses `add references`); on a flatten of a single
            // layer stack `add` and `prepend` produce the same result, and
            // `prepend` takes the supported InheritPrimSpec path.
            if (arcsPair.first == tinyusdz::ListEditQual::Add) arcsPair.first = tinyusdz::ListEditQual::Prepend;
            for (auto& arc : arcsPair.second) {
                if (arc.asset_path.GetAssetPath().empty()) continue;// internal (inherit-like) arc
                if (arc.prim_path.is_valid()) continue;// already explicit
                const auto resolved = PeekResolve(arc.asset_path.GetAssetPath(), ps, baseDir);
                if (!resolved) continue;
                tinyusdz::Path dp;
                if (PeekDefaultPrim(*resolved, &dp, cache)) arc.prim_path = dp;
            }
        };
        if (ps.metas().references) fix(ps.metas().references.value());
        if (ps.metas().payload) fix(ps.metas().payload.value());
        for (auto& c : ps.children())
            FillArcPrimPaths(c, baseDir, cache);
    }

    // ── Post-composition dangling-target repair ──────────────────────────────
    // tinyusdz retargets a referenced layer's internal paths (`ReplaceRootPrimPathRec`)
    // but does NOT descend into variantSet content, so a `material:binding` (and
    // the material's shader `.connect`) authored inside a variant keeps pointing
    // at the pre-flatten namespace after the variant is composed — the mesh loads
    // but its material is dropped. Kitchen_set wraps every prop in a
    // modelingVariant, which is why it flattened to thousands of meshes yet zero
    // materials. Repair any still-dangling absolute target here: strip its root
    // component and rebind it to the matching subtree under the nearest ancestor
    // of the holding prim that actually exists. Only unresolved targets are
    // touched, so correctly-retargeted paths are left alone.
    void CollectPrimPaths(const tinyusdz::PrimSpec& ps, const std::string& parent, std::set<std::string>& out) {
        const std::string path = parent + "/" + ps.name();
        out.insert(path);
        for (const auto& c : ps.children())
            CollectPrimPaths(c, path, out);
    }

    void RetargetDangling(tinyusdz::Path& target, const std::string& holder, const std::set<std::string>& prims) {
        if (!target.is_absolute_path()) return;
        const std::string t = target.prim_part();
        if (t.empty() || t == "/" || prims.count(t)) return;// unresolved only
        const size_t split = t.find('/', 1);
        if (split == std::string::npos) return;
        const std::string rootComp = t.substr(0, split);// e.g. "/Prop"
        const std::string suffix = t.substr(split);     // e.g. "/Looks/Mat"
        std::string anc = holder;
        while (true) {
            if (prims.count(anc + suffix)) {
                target.replace_prefix(tinyusdz::Path(rootComp, ""), tinyusdz::Path(anc, ""));
                return;
            }
            const size_t s = anc.find_last_of('/');
            if (s == std::string::npos || s == 0) break;
            anc = anc.substr(0, s);
        }
    }

    void FixDanglingTargets(tinyusdz::PrimSpec& ps, const std::string& parent, const std::set<std::string>& prims) {
        const std::string self = parent + "/" + ps.name();
        for (auto& [name, prop] : ps.props()) {
            if (prop.is_relationship()) {
                auto& rel = prop.relationship();
                if (rel.is_path()) {
                    RetargetDangling(rel.targetPath, self, prims);
                } else if (rel.is_pathvector()) {
                    for (auto& p : rel.targetPathVector)
                        RetargetDangling(p, self, prims);
                }
            } else if (prop.is_attribute_connection()) {
                for (auto& c : prop.attribute().connections())
                    RetargetDangling(c, self, prims);
            }
        }
        for (auto& c : ps.children())
            FixDanglingTargets(c, self, prims);
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
        if (!tinyusdz::LoadLayerFromMemory(bytes.data(), bytes.size(), virtualPath, &root, warn, err)) return false;

        tinyusdz::AssetResolutionResolver resolver;
        resolver.register_wildcard_asset_resolution_handler(MakeFsHandler(ctx));
        ctx->resolver = &resolver;// so FsResolve can read current_working_path()
        const std::string cwd = baseDir.empty() ? "." : baseDir;
        resolver.set_search_paths({ cwd });
        resolver.set_current_working_path(cwd);

        tinyusdz::Layer src = root;
        {
            tinyusdz::Layer composited;
            if (tinyusdz::CompositeSublayers(resolver, src, &composited, warn, err)) src = std::move(composited);
        }
        // References/payloads can nest (a referenced layer references more); loop
        // until no arc remains unresolved (bounded to avoid a pathological cycle).
        constexpr int kMaxIterations = 128;
        std::map<std::string, tinyusdz::Path> defaultPrimCache;
        for (int i = 0; i < kMaxIterations; ++i) {
            bool unresolved = false;
            // Supply defaultPrim as the arc prefix before flattening this level so
            // tinyusdz retargets the sub-layer's internal binding/connection paths.
            for (auto& kv : src.primspecs())
                FillArcPrimPaths(kv.second, baseDir, defaultPrimCache);
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
            // Defer variant composition until references/payloads are fully
            // settled. Kitchen_set-style props author the variant *selection* on
            // an outer prim whose variantSet blocks are empty shells; the
            // populated blocks arrive from a deeper reference→payload→reference
            // chain. Consuming the selection while the blocks are still empty
            // destroys it, and the late-arriving content (the prop's Looks +
            // material bindings) is dropped with it — meshes import, materials
            // vanish. Variant content can itself introduce new arcs, so the
            // loop keeps iterating afterwards.
            if (unresolved) continue;
            if (src.check_unresolved_variant()) {
                unresolved = true;
                tinyusdz::Layer composited;
                if (tinyusdz::CompositeVariant(src, &composited, warn, err)) src = std::move(composited);
            }
            if (!unresolved) break;
        }

        ctx->resolver = nullptr;// `resolver` dies with this scope — don't dangle

        // Rebind any binding/connection paths left dangling by variant composition.
        {
            std::set<std::string> prims;
            for (auto& [name, ps] : src.primspecs())
                CollectPrimPaths(ps, "", prims);
            for (auto& [name, ps] : src.primspecs())
                FixDanglingTargets(ps, "", prims);
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
    const std::string baseDir = FileSystem::DirName(path);

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
    const bool loaded = isUsdz ? tinyusdz::LoadUSDFromMemory(bytes.data(), bytes.size(), realPath, &stage, &warn, &err)
                               : ComposeStage(bytes, path, baseDir, &fsctx, &stage, &warn, &err);
    if (!loaded) {
        if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));
        if (!err.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}' error: {}", path, err));
        return Prefab{};
    }
    if (!warn.empty()) ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab '{}': {}", path, warn));

    // Debug: set AE_USD_DUMP=1 to write the flattened stage next to the source as
    // `<name>.flat.usda` for inspection (e.g. why a material didn't survive).
    if (std::getenv("AE_USD_DUMP")) {
        const std::string dumpPath =
            (FileSystem::Get().BasePath().empty() ? std::string() : FileSystem::Get().BasePath()) + path + ".flat.usda";
        std::string dw, de;
        if (tinyusdz::usda::SaveAsUSDA(dumpPath, stage, &dw, &de))
            ConsoleSubsystem::Get()->Info(fmt::format("ImportUSDPrefab: dumped flattened stage to '{}'", dumpPath));
        else
            ConsoleSubsystem::Get()->Warn(fmt::format("ImportUSDPrefab: stage dump failed: {}", de));
    }

    tinyusdz::tydra::RenderSceneConverter converter;
    tinyusdz::tydra::RenderSceneConverterEnv env(stage);
    if (isUsdz) {
        // USDZ textures are embedded in the archive and resolved by tinyusdz's
        // own asset map; the absolute base dir is the search root as before.
        const std::string dir = FileSystem::DirName(realPath);
        if (!dir.empty()) env.set_search_paths({ dir });
    } else {
        // Resolve material textures through the same FileSystem-backed handler.
        // Search the root dir plus every sub-asset directory pulled in during
        // composition (fsctx keys), so textures beside deeply-nested sub-USDs
        // (Kitchen_set scatters them across assets/*/) are found.
        env.asset_resolver.register_wildcard_asset_resolution_handler(MakeFsHandler(&fsctx));
        fsctx.resolver = &env.asset_resolver;// FsResolve reads its cwp for textures
        std::vector<std::string> searchPaths;
        if (!baseDir.empty()) searchPaths.push_back(baseDir);
        for (const auto& [key, _] : fsctx.cache) {
            std::string dir = FileSystem::DirName(key);
            if (!dir.empty()) searchPaths.push_back(std::move(dir));
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
    // Meshes with no bound material fall back to their `displayColor` primvar,
    // synthesized into a shared PrefabMaterial per distinct color. This is how
    // pre-UsdPreviewSurface assets (e.g. Pixar's 2016 Kitchen_set, which has no
    // UsdShade materials at all — usdview colors it from displayColor) keep
    // their authored look. Tydra puts a 1-element displayColor primvar into
    // RenderMesh::displayColor (default 0.18 grey, usdview's fallback too).
    std::map<std::tuple<int, int, int>, int> displayColorMats;
    auto displayColorMaterial = [&](const tinyusdz::tydra::RenderMesh& rmesh) -> int {
        const glm::vec3 c(rmesh.displayColor[0], rmesh.displayColor[1], rmesh.displayColor[2]);
        const auto key = std::make_tuple(
            static_cast<int>(c.r * 255.0f + 0.5f),
            static_cast<int>(c.g * 255.0f + 0.5f),
            static_cast<int>(c.b * 255.0f + 0.5f)
        );
        auto it = displayColorMats.find(key);
        if (it != displayColorMats.end()) return it->second;
        PrefabMaterial pm;
        pm.name = fmt::format(
            "displayColor_{:02x}{:02x}{:02x}",
            std::get<0>(key),
            std::get<1>(key),
            std::get<2>(key)
        );
        pm.baseColor = c;
        pm.roughness = 0.8f;// matte, close to usdview's default shading
        pm.metallic = 0.0f;
        const int index = static_cast<int>(out.materials.size());
        out.materials.push_back(std::move(pm));
        displayColorMats.emplace(key, index);
        return index;
    };

    std::vector<std::vector<int>> meshChunks(rscene.meshes.size());
    for (size_t i = 0; i < rscene.meshes.size(); ++i) {
        const auto& rmesh = rscene.meshes[i];
        // Tydra records the bound material per mesh via materialMap; RenderMesh
        // carries material_id in recent versions — resolve defensively.
        int matIndex = -1;
        std::string matName;
        if (rmesh.material_id >= 0 && rmesh.material_id < static_cast<int>(rscene.materials.size())) {
            matIndex = rmesh.material_id;
            matName = out.materials[matIndex].name;
        } else {
            matIndex = displayColorMaterial(rmesh);
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

    out.root.name = FileSystem::BaseName(realPath);
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
    // Diagnostic: how many Material prims survive in the composed stage vs how
    // many Tydra actually bound. stageMaterials == 0 means composition/parse
    // dropped them; stageMaterials > 0 with 0 converted means the bindings did
    // not resolve. (Helps triage USD material issues without a rebuild.)
    tinyusdz::tydra::PathPrimMap<tinyusdz::Material> stageMaterials;
    tinyusdz::tydra::ListPrims(stage, stageMaterials);
    ConsoleSubsystem::Get()->Info(
        fmt::format(
            "ImportUSDPrefab '{}': {} mesh(es), {} material(s), {} image(s) "
            "[stage: {} Material prim(s)]",
            path,
            out.meshes.size(),
            out.materials.size(),
            out.images.size(),
            stageMaterials.size()
        )
    );
    return out;
}

#endif// AE_USE_TINYUSDZ

// glTF / GLB → Prefab importer (geometry, node hierarchy, PBR materials with
// decoded textures, and the common KHR extensions). Pure CPU — image decode
// happens here (stb via tinygltf's loader hook) but GPU upload is Instantiate's
// job. The single-mesh textured LoadGLTF path in asset_manager.cpp is separate.
//
// Supported extensions:
//   KHR_lights_punctual            → PrefabLight on the owning node
//   KHR_texture_transform          → baked into UV0 at import (baseColor's
//                                    transform, applied per primitive)
//   KHR_materials_emissive_strength→ multiplied into PrefabMaterial::emissive
//   KHR_materials_transmission     → PrefabMaterial::transmissionFactor + texture
//   KHR_materials_volume           → thicknessFactor, attenuationDistance/Color
//   KHR_materials_ior              → PrefabMaterial::ior
//     (transmission/volume/ior are imported as data only — no shader consumes
//      them yet; a refraction pass is separate. Defaults = opaque dielectric.)
// Unsupported (skipped, geometry still imports): Draco/meshopt compression,
// specular-glossiness materials, skinning/morph targets.
#include "console_subsystem.hpp"
#include "file_system.hpp"
#include "logging.hpp"
#include "prefab.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "fmt/core.h"
#include "stb_image.h"
// Match asset_manager.cpp's tinygltf config (the TINYGLTF_IMPLEMENTATION TU):
// it builds with TINYGLTF_NO_STB_IMAGE_WRITE, so WriteImageData is never
// compiled. Without this define here, tiny_gltf.h in this TU takes the address
// of tinygltf::WriteImageData as the loader's default image-writer callback,
// leaving an undefined symbol at link (we only read glTF, never write).
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#ifdef _WIN32
#undef LoadImage
#endif

namespace {

    glm::mat4 GLTFNodeMatrix(const tinygltf::Node& n) {
        if (n.matrix.size() == 16) {
            glm::mat4 m(1.0f);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    m[c][r] = static_cast<float>(n.matrix[c * 4 + r]);
            return m;
        }
        glm::mat4 m(1.0f);
        if (n.translation.size() == 3)
            m = glm::translate(m, glm::vec3(n.translation[0], n.translation[1], n.translation[2]));
        if (n.rotation.size() == 4) {
            glm::quat q(
                static_cast<float>(n.rotation[3]),
                static_cast<float>(n.rotation[0]),
                static_cast<float>(n.rotation[1]),
                static_cast<float>(n.rotation[2])
            );
            m = m * glm::mat4_cast(q);
        }
        if (n.scale.size() == 3) m = glm::scale(m, glm::vec3(n.scale[0], n.scale[1], n.scale[2]));
        return m;
    }

    // KHR_texture_transform payload (per glTF spec: uv' = T * R * S * uv).
    struct UVTransform {
        glm::vec2 offset{ 0.0f };
        glm::vec2 scale{ 1.0f };
        float rotation = 0.0f;
        bool active = false;

        glm::vec2 Apply(glm::vec2 uv) const {
            if (!active) return uv;
            const float c = std::cos(rotation), s = std::sin(rotation);
            const glm::vec2 scaled(uv.x * scale.x, uv.y * scale.y);
            return { c * scaled.x - s * scaled.y + offset.x, s * scaled.x + c * scaled.y + offset.y };
        }
    };

    UVTransform ReadTextureTransform(const tinygltf::ExtensionMap& ext) {
        UVTransform t;
        auto it = ext.find("KHR_texture_transform");
        if (it == ext.end() || !it->second.IsObject()) return t;
        t.active = true;
        const tinygltf::Value& v = it->second;
        if (v.Has("offset") && v.Get("offset").ArrayLen() == 2)
            t.offset = { v.Get("offset").Get(0).GetNumberAsDouble(), v.Get("offset").Get(1).GetNumberAsDouble() };
        if (v.Has("scale") && v.Get("scale").ArrayLen() == 2)
            t.scale = { v.Get("scale").Get(0).GetNumberAsDouble(), v.Get("scale").Get(1).GetNumberAsDouble() };
        if (v.Has("rotation")) t.rotation = static_cast<float>(v.Get("rotation").GetNumberAsDouble());
        return t;
    }

    // node.light for KHR_lights_punctual (kept out of the header dependency).
    int NodeLightIndex(const tinygltf::Node& n) {
        if (n.light >= 0) return n.light;
        auto it = n.extensions.find("KHR_lights_punctual");
        if (it != n.extensions.end() && it->second.Has("light")) return it->second.Get("light").GetNumberAsInt();
        return -1;
    }

}// namespace

Prefab ImportGLTFPrefab(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    // Decode embedded/external images to RGBA8, flipped for the GL UV origin
    // (matches LoadGLTF so both paths render textures the same way up).
    loader.SetImageLoader(
        [](tinygltf::Image* img,
           const int,
           std::string*,
           std::string*,
           int,
           int,
           const unsigned char* bytes,
           int size,
           void*) -> bool {
            int w, h, c;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &c, 4);
            stbi_set_flip_vertically_on_load(false);
            if (!data) return false;
            img->width = w;
            img->height = h;
            img->component = 4;
            img->image.assign(data, data + static_cast<size_t>(w) * h * 4);
            stbi_image_free(data);
            return true;
        },
        nullptr
    );

    // Read the bytes through FileSystem, not tinygltf's own fopen(): on web the
    // file is fetched into the FileSystem cache (keyed by this relative path) and
    // never touches a real filesystem, so LoadASCII/BinaryFromFile's fopen()
    // fails. baseDir (dir of realPath) is the search path tinygltf uses for
    // external .bin/textures; the committed sample embeds its buffer, so it is
    // only relevant to on-disk (native) assets with external dependencies.
    const std::string realPath = FileSystem::Get().ResolvePath(path).value_or(path);
    const std::string baseDir = FileSystem::DirName(realPath);
    FileSystem::Bytes bytes = FileSystem::Get().ReadSync(path);
    if (bytes.empty()) {
        ENGINE_WARN("ImportGLTFPrefab '{}': file not found (native) or not prefetched (web)", path);
        return Prefab{};
    }
    const bool result = path.ends_with(".glb")
                            ? loader.LoadBinaryFromMemory(
                                  &model, &err, &warn, bytes.data(), static_cast<unsigned int>(bytes.size()), baseDir
                              )
                            : loader.LoadASCIIFromString(
                                  &model,
                                  &err,
                                  &warn,
                                  reinterpret_cast<const char*>(bytes.data()),
                                  static_cast<unsigned int>(bytes.size()),
                                  baseDir
                              );
    if (!warn.empty()) ENGINE_WARN("ImportGLTFPrefab '{}': {}", path, warn);
    if (!err.empty()) ENGINE_WARN("ImportGLTFPrefab '{}' error: {}", path, err);
    if (!result) return Prefab{};

    Prefab out;

    // ── Images ────────────────────────────────────────────────────────────────
    for (size_t i = 0; i < model.images.size(); ++i) {
        const auto& img = model.images[i];
        PrefabImage pi;
        pi.name = img.uri.empty() ? fmt::format("{}#image{}", path, i) : img.uri;
        pi.width = img.width;
        pi.height = img.height;
        pi.channels = img.component;
        pi.pixels = img.image;
        out.images.push_back(std::move(pi));
    }
    // texture index -> image index (through the sampler-less glTF texture table)
    auto imageOf = [&](int texIndex) -> int {
        if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) return -1;
        const int src = model.textures[texIndex].source;
        return (src >= 0 && src < static_cast<int>(out.images.size())) ? src : -1;
    };

    // ── Materials (+ per-material baseColor UV transform to bake) ────────────
    std::vector<UVTransform> matUV(model.materials.size());
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& gm = model.materials[i];
        PrefabMaterial pm;
        pm.name = gm.name.empty() ? fmt::format("gltf_mat_{}", i) : gm.name;
        const auto& pbr = gm.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() == 4)
            pm.baseColor = { pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2] };
        pm.metallic = static_cast<float>(pbr.metallicFactor);
        pm.roughness = static_cast<float>(pbr.roughnessFactor);
        pm.doubleSided = gm.doubleSided;
        pm.baseColorImage = imageOf(pbr.baseColorTexture.index);
        pm.metallicRoughnessImage = imageOf(pbr.metallicRoughnessTexture.index);
        pm.normalImage = imageOf(gm.normalTexture.index);
        pm.occlusionImage = imageOf(gm.occlusionTexture.index);
        pm.emissiveImage = imageOf(gm.emissiveTexture.index);
        if (gm.emissiveFactor.size() == 3)
            pm.emissive = { gm.emissiveFactor[0], gm.emissiveFactor[1], gm.emissiveFactor[2] };
        // KHR_materials_emissive_strength scales the emissive factor.
        if (auto es = gm.extensions.find("KHR_materials_emissive_strength"); es != gm.extensions.end())
            if (es->second.Has("emissiveStrength"))
                pm.emissive *= static_cast<float>(es->second.Get("emissiveStrength").GetNumberAsDouble());
        // KHR_materials_ior — index of refraction (default 1.5 when absent).
        if (auto ex = gm.extensions.find("KHR_materials_ior"); ex != gm.extensions.end())
            if (ex->second.Has("ior")) pm.ior = static_cast<float>(ex->second.Get("ior").GetNumberAsDouble());
        // KHR_materials_transmission — base transmission factor + optional (R) texture.
        if (auto ex = gm.extensions.find("KHR_materials_transmission"); ex != gm.extensions.end()) {
            const auto& e = ex->second;
            if (e.Has("transmissionFactor"))
                pm.transmissionFactor = static_cast<float>(e.Get("transmissionFactor").GetNumberAsDouble());
            if (e.Has("transmissionTexture") && e.Get("transmissionTexture").Has("index"))
                pm.transmissionImage = imageOf(e.Get("transmissionTexture").Get("index").GetNumberAsInt());
        }
        // KHR_materials_volume — thickness (G) + Beer-Lambert absorption. Only
        // meaningful with thicknessFactor > 0; attenuationDistance stays +inf
        // (no absorption) when the key is absent.
        if (auto ex = gm.extensions.find("KHR_materials_volume"); ex != gm.extensions.end()) {
            const auto& e = ex->second;
            if (e.Has("thicknessFactor"))
                pm.thicknessFactor = static_cast<float>(e.Get("thicknessFactor").GetNumberAsDouble());
            if (e.Has("thicknessTexture") && e.Get("thicknessTexture").Has("index"))
                pm.thicknessImage = imageOf(e.Get("thicknessTexture").Get("index").GetNumberAsInt());
            if (e.Has("attenuationDistance"))
                pm.attenuationDistance = static_cast<float>(e.Get("attenuationDistance").GetNumberAsDouble());
            if (e.Has("attenuationColor") && e.Get("attenuationColor").IsArray()
                && e.Get("attenuationColor").ArrayLen() == 3) {
                const auto& c = e.Get("attenuationColor");
                pm.attenuationColor = { static_cast<float>(c.Get(0).GetNumberAsDouble()),
                                        static_cast<float>(c.Get(1).GetNumberAsDouble()),
                                        static_cast<float>(c.Get(2).GetNumberAsDouble()) };
            }
        }
        matUV[i] = ReadTextureTransform(pbr.baseColorTexture.extensions);
        out.materials.push_back(std::move(pm));
    }

    // ── Lights (KHR_lights_punctual) ─────────────────────────────────────────
    for (const auto& gl : model.lights) {
        PrefabLight pl;
        pl.type = gl.type == "directional" ? PrefabLight::Type::Directional
                  : gl.type == "spot"      ? PrefabLight::Type::Spot
                                           : PrefabLight::Type::Point;
        if (gl.color.size() == 3) pl.color = { gl.color[0], gl.color[1], gl.color[2] };
        pl.intensity = static_cast<float>(gl.intensity);
        pl.range = static_cast<float>(gl.range);
        out.lights.push_back(pl);
    }

    // ── Accessor readers (byteStride-aware, same semantics as LoadGLTF) ──────
    auto readFloat3 = [&](const tinygltf::Accessor& acc) {
        const auto& bv = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 3;
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec3> o(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            const auto* v = reinterpret_cast<const float*>(base + i * stride);
            o[i] = { v[0], v[1], v[2] };
        }
        return o;
    };
    auto readFloat4 = [&](const tinygltf::Accessor& acc) {
        const auto& bv = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 4;
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec4> o(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            const auto* v = reinterpret_cast<const float*>(base + i * stride);
            o[i] = { v[0], v[1], v[2], v[3] };
        }
        return o;
    };
    auto readTexcoord = [&](const tinygltf::Accessor& acc) {
        const auto& bv = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        std::vector<glm::vec2> o(acc.count);
        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            const size_t stride = bv.byteStride ? bv.byteStride : sizeof(float) * 2;
            for (size_t i = 0; i < acc.count; ++i) {
                const auto* v = reinterpret_cast<const float*>(base + i * stride);
                o[i] = { v[0], v[1] };
            }
        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const size_t stride = bv.byteStride ? bv.byteStride : 2u;
            for (size_t i = 0; i < acc.count; ++i) {
                const uint8_t* v = base + i * stride;
                o[i] = { v[0] / 255.0f, v[1] / 255.0f };
            }
        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const size_t stride = bv.byteStride ? bv.byteStride : sizeof(uint16_t) * 2;
            for (size_t i = 0; i < acc.count; ++i) {
                const auto* v = reinterpret_cast<const uint16_t*>(base + i * stride);
                o[i] = { v[0] / 65535.0f, v[1] / 65535.0f };
            }
        }
        return o;
    };
    auto readIndices = [&](const tinygltf::Primitive& prim, size_t vertCount) {
        std::vector<uint32_t> idx;
        if (prim.indices < 0) {
            idx.resize(vertCount);
            for (size_t i = 0; i < vertCount; ++i)
                idx[i] = static_cast<uint32_t>(i);
            return idx;
        }
        const auto& acc = model.accessors[prim.indices];
        const auto& bv = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
        idx.reserve(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            uint32_t v = 0;
            if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                std::memcpy(&v, base + i * sizeof(uint32_t), sizeof(uint32_t));
            else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                uint16_t s;
                std::memcpy(&s, base + i * sizeof(uint16_t), sizeof(uint16_t));
                v = s;
            } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                v = base[i];
            idx.push_back(v);
        }
        return idx;
    };

    // ── Primitives → MeshData (chunked past the 16-bit ceiling) ──────────────
    std::vector<std::vector<int>> meshPrims(model.meshes.size());
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        for (const auto& prim : model.meshes[mi].primitives) {
            if (!prim.attributes.contains("POSITION")) continue;
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) continue;// tri lists only
            const auto& posAcc = model.accessors[prim.attributes.at("POSITION")];
            const size_t vertCount = posAcc.count;
            if (vertCount == 0) continue;

            auto positions = readFloat3(posAcc);
            std::vector<glm::vec3> normals(vertCount, glm::vec3(0, 1, 0));
            if (prim.attributes.contains("NORMAL")) normals = readFloat3(model.accessors[prim.attributes.at("NORMAL")]);
            std::vector<glm::vec2> uvs(vertCount, glm::vec2(0));
            if (prim.attributes.contains("TEXCOORD_0"))
                uvs = readTexcoord(model.accessors[prim.attributes.at("TEXCOORD_0")]);
            std::vector<glm::vec4> tangents4(vertCount, glm::vec4(1, 0, 0, 1));
            if (prim.attributes.contains("TANGENT"))
                tangents4 = readFloat4(model.accessors[prim.attributes.at("TANGENT")]);

            // Bake the material's baseColor KHR_texture_transform into UV0.
            const UVTransform* uvt = nullptr;
            if (prim.material >= 0 && prim.material < static_cast<int>(matUV.size()) && matUV[prim.material].active)
                uvt = &matUV[prim.material];

            const std::vector<uint32_t> srcIdx = readIndices(prim, vertCount);

            // Chunk triangles into <=65535-vertex MeshData pieces (uint32 remap,
            // same walk as SplitMeshData but from source accessors).
            MeshData cur;
            std::unordered_map<uint32_t, uint16_t> remap;
            auto beginChunk = [&]() {
                cur = MeshData{};
                cur.materialIndex = prim.material;
                if (prim.material >= 0 && prim.material < static_cast<int>(out.materials.size()))
                    cur.material = out.materials[prim.material].name;
                remap.clear();
            };
            auto flushChunk = [&]() {
                if (cur.vertices.empty()) return;
                meshPrims[mi].push_back(static_cast<int>(out.meshes.size()));
                out.meshes.push_back(std::move(cur));
            };
            beginChunk();
            for (size_t t = 0; t + 2 < srcIdx.size(); t += 3) {
                if (cur.vertices.size() + 3 > 65535) {
                    flushChunk();
                    beginChunk();
                }
                for (int k = 0; k < 3; ++k) {
                    const uint32_t s = srcIdx[t + k];
                    if (s >= vertCount) continue;
                    auto it = remap.find(s);
                    uint16_t local;
                    if (it != remap.end()) {
                        local = it->second;
                    } else {
                        Vertex v;
                        v.position = positions[s];
                        v.normal = normals[s];
                        // glTF UVs are top-left origin; the engine flips images
                        // on load (GraphicsSubsystem sets stbi flip-Y globally)
                        // and authors UVs bottom-origin (see mesh_builder's
                        // 1-v), so flip V here to match. KHR_texture_transform
                        // is applied first, in glTF UV space.
                        v.uv = uvt ? uvt->Apply(uvs[s]) : uvs[s];
                        v.uv.y = 1.0f - v.uv.y;
                        const glm::vec3 tan = glm::vec3(tangents4[s]);
                        v.tangent = tan;
                        v.bitangent = glm::cross(normals[s], tan) * tangents4[s].w;
                        local = static_cast<uint16_t>(cur.vertices.size());
                        cur.vertices.push_back(v);
                        remap[s] = local;
                    }
                    cur.indices.push_back(local);
                }
            }
            flushChunk();
        }
    }

    // ── Node tree ─────────────────────────────────────────────────────────────
    std::function<PrefabNode(int)> buildNode = [&](int nodeIdx) -> PrefabNode {
        const tinygltf::Node& n = model.nodes[nodeIdx];
        PrefabNode node;
        node.name = n.name.empty() ? fmt::format("node_{}", nodeIdx) : n.name;
        node.transform = GLTFNodeMatrix(n);
        if (n.mesh >= 0 && n.mesh < static_cast<int>(meshPrims.size())) node.meshes = meshPrims[n.mesh];
        const int lightIdx = NodeLightIndex(n);
        if (lightIdx >= 0 && lightIdx < static_cast<int>(out.lights.size())) node.lights.push_back(lightIdx);
        for (int c : n.children)
            if (c >= 0 && c < static_cast<int>(model.nodes.size())) node.children.push_back(buildNode(c));
        return node;
    };

    out.root.name = FileSystem::BaseName(path);
    const int sceneIdx = (model.defaultScene >= 0) ? model.defaultScene : 0;
    if (sceneIdx < static_cast<int>(model.scenes.size())) {
        for (int rootNode : model.scenes[sceneIdx].nodes)
            if (rootNode >= 0 && rootNode < static_cast<int>(model.nodes.size()))
                out.root.children.push_back(buildNode(rootNode));
    } else {
        for (const auto& prims : meshPrims)
            for (int mp : prims)
                out.root.meshes.push_back(mp);
    }

    out.ok = !out.meshes.empty();
    return out;
}

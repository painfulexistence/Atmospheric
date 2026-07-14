#include "prefab.hpp"
#include "file_system.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <sstream>

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

// ─────────────────────────────────────────────────────────────────────────────
// TrenchBroom / Quake ".map" importer (pure CPU → Prefab)
//
// A .map is a list of entities `{ … }`. A brush entity contains brushes `{ … }`
// in one of three face formats — classic idTech2 (`TEX xoff yoff rot sx sy`),
// Valve 220 (`TEX [ux uy uz uoff] [vx vy vz voff] rot sx sy`), or idTech3
// brush primitives (`brushDef` with a 2x3 texture matrix) — plus `patchDef2`
// biquadratic Bezier patches. A brush is the convex volume carved out by the
// intersection of every face's half-space, so a face's polygon is recovered by
// clipping a huge quad on that plane against all the other planes (as qbsp
// does). Every entity's full key/value block is preserved, point entities
// included, so gameplay can query spawn data (Prefab::FindEntities).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

    enum class FaceUV { Classic, Valve220, BrushPrimitive };

    struct MapFace {
        glm::dvec3 normal{ 0 };// outward plane normal (Quake space)
        double dist = 0;// plane offset: dot(normal, x) == dist on the plane
        std::string texture;
        FaceUV uvMode = FaceUV::Classic;
        // Classic / Valve 220
        glm::dvec3 uAxis{ 1, 0, 0 }, vAxis{ 0, 1, 0 };
        double uOffset = 0, vOffset = 0;
        double rotation = 0, scaleX = 1, scaleY = 1;
        // Brush primitives: 2x3 texture matrix (rows map plane-space -> UV)
        double texMat[2][3] = { { 1, 0, 0 }, { 0, 1, 0 } };
    };

    struct MapBrush {
        std::vector<MapFace> faces;
    };

    struct MapPatchVert {
        glm::dvec3 pos{ 0 };
        glm::dvec2 st{ 0 };
    };

    struct MapPatch {
        std::string texture;
        int width = 0, height = 0;
        std::vector<MapPatchVert> points;// width*height, row-major [col*height + row] as parsed
    };

    struct MapEntity {
        std::string classname;
        std::unordered_map<std::string, std::string> properties;
        std::vector<MapBrush> brushes;
        std::vector<MapPatch> patches;
    };

    // Split the file into tokens, treating braces/parens/brackets as single
    // characters, keeping "quoted strings" intact, and dropping // line comments.
    std::vector<std::string> TokenizeMap(const std::string& text) {
        std::vector<std::string> tokens;
        size_t i = 0, n = text.size();
        while (i < n) {
            char c = text[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
            } else if (c == '/' && i + 1 < n && text[i + 1] == '/') {
                while (i < n && text[i] != '\n')
                    ++i;
            } else if (c == '"') {
                size_t j = i + 1;
                while (j < n && text[j] != '"')
                    ++j;
                tokens.emplace_back(text.substr(i + 1, j - i - 1));
                i = (j < n) ? j + 1 : j;
            } else if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
                tokens.emplace_back(1, c);
                ++i;
            } else {
                size_t j = i;
                while (j < n && !std::isspace(static_cast<unsigned char>(text[j])) && text[j] != '{' && text[j] != '}'
                       && text[j] != '(' && text[j] != ')' && text[j] != '[' && text[j] != ']')
                    ++j;
                tokens.emplace_back(text.substr(i, j - i));
                i = j;
            }
        }
        return tokens;
    }

    double ParseDouble(const std::string& s) {
        try {
            return std::stod(s);
        } catch (...) {
            return 0.0;
        }
    }

    // Classic-format texture axes, indexed by dominant face-normal direction.
    // Layout per entry: { normal, uAxis, vAxis }. From Quake's map.c.
    const glm::dvec3 kBaseAxis[18] = {
        { 0, 0, 1 },  { 1, 0, 0 }, { 0, -1, 0 },// floor
        { 0, 0, -1 }, { 1, 0, 0 }, { 0, -1, 0 },// ceiling
        { 1, 0, 0 },  { 0, 1, 0 }, { 0, 0, -1 },// west wall
        { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 },// east wall
        { 0, 1, 0 },  { 1, 0, 0 }, { 0, 0, -1 },// south wall
        { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }// north wall
    };

    // Brush-primitives plane-space basis, per GtkRadiant/q3map2's ComputeAxisBase:
    // a fixed 2D frame on the face plane derived from the normal alone, which the
    // brushDef texture matrix then maps to UV.
    void ComputeAxisBase(glm::dvec3 normal, glm::dvec3& texX, glm::dvec3& texY) {
        // Clean near-zero components exactly as radiant does (avoids atan2 noise).
        for (int i = 0; i < 3; ++i)
            if (std::abs(normal[i]) < 1e-6) normal[i] = 0.0;
        const double rotY = -std::atan2(normal.z, std::sqrt(normal.x * normal.x + normal.y * normal.y));
        const double rotZ = std::atan2(normal.y, normal.x);
        texX = { -std::sin(rotZ), std::cos(rotZ), 0.0 };
        texY = { -std::sin(rotY) * std::cos(rotZ), -std::sin(rotY) * std::sin(rotZ), -std::cos(rotY) };
    }

    // Sutherland-Hodgman clip: keep the part of `poly` on the inside (dist <= 0)
    // half-space of the plane (normal, d).
    std::vector<glm::dvec3> ClipPolygon(const std::vector<glm::dvec3>& poly, glm::dvec3 normal, double d) {
        constexpr double kEps = 1e-4;
        std::vector<glm::dvec3> out;
        const size_t m = poly.size();
        for (size_t i = 0; i < m; ++i) {
            const glm::dvec3& a = poly[i];
            const glm::dvec3& b = poly[(i + 1) % m];
            double da = glm::dot(normal, a) - d;
            double db = glm::dot(normal, b) - d;
            bool aIn = da <= kEps;
            bool bIn = db <= kEps;
            if (aIn) out.push_back(a);
            if (aIn != bIn) {
                double t = da / (da - db);
                out.push_back(a + t * (b - a));
            }
        }
        return out;
    }

    // Convert Quake Z-up to the engine's Y-up (a proper rotation, so winding is
    // preserved): (x, y, z) → (x, z, -y).
    inline glm::vec3 QuakeToEngine(glm::dvec3 v) {
        return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.z), static_cast<float>(-v.y));
    }

    std::string ToLower(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Special-texture classification (name checks cover both bare and
    // "common/..."-prefixed forms used by Q3/TB texture sets).
    bool TexContains(const std::string& tex, const char* what) {
        return ToLower(tex).find(what) != std::string::npos;
    }
    // Face is never rendered (but its brush stays solid).
    bool IsNonRenderTexture(const std::string& t) {
        return TexContains(t, "nodraw") || TexContains(t, "caulk") || TexContains(t, "skip") || TexContains(t, "hint")
               || TexContains(t, "areaportal") || TexContains(t, "clip") || TexContains(t, "trigger")
               || TexContains(t, "origin");
    }
    // Brush contributes no collider at all.
    bool IsNonSolidTexture(const std::string& t) {
        return TexContains(t, "hint") || TexContains(t, "areaportal") || TexContains(t, "trigger")
               || TexContains(t, "origin");
    }

    // ── Face parsing ─────────────────────────────────────────────────────────

    // Parses one face line starting at tokens[pos] (which must be "("). Returns
    // false when the cursor isn't at a face. Advances pos past the face.
    bool ParseFace(const std::vector<std::string>& tk, size_t& pos, MapFace& face, bool brushPrimitives) {
        const size_t count = tk.size();
        glm::dvec3 p[3];
        for (auto& pt : p) {
            if (pos >= count || tk[pos] != "(") return false;
            ++pos;
            pt.x = ParseDouble(tk[pos < count ? pos : count - 1]);
            pt.y = ParseDouble(tk[pos + 1 < count ? pos + 1 : count - 1]);
            pt.z = ParseDouble(tk[pos + 2 < count ? pos + 2 : count - 1]);
            pos += 3;
            if (pos >= count || tk[pos] != ")") return false;
            ++pos;
        }
        face.normal = glm::normalize(glm::cross(p[0] - p[1], p[2] - p[1]));
        face.dist = glm::dot(face.normal, p[0]);

        if (brushPrimitives) {
            // ( ( m00 m01 m02 ) ( m10 m11 m12 ) ) TEX [contents flags value]
            face.uvMode = FaceUV::BrushPrimitive;
            if (pos < count && tk[pos] == "(") ++pos;
            for (int r = 0; r < 2; ++r) {
                if (pos < count && tk[pos] == "(") ++pos;
                for (int c = 0; c < 3; ++c)
                    face.texMat[r][c] = ParseDouble(tk[pos + c < count ? pos + c : count - 1]);
                pos += 3;
                if (pos < count && tk[pos] == ")") ++pos;
            }
            if (pos < count && tk[pos] == ")") ++pos;
            face.texture = (pos < count) ? tk[pos++] : "";
            // Optional trailing surface flags (up to 3 numeric tokens).
            for (int i = 0; i < 3 && pos < count && tk[pos] != "(" && tk[pos] != "}"; ++i)
                ++pos;
            return true;
        }

        face.texture = (pos < count) ? tk[pos++] : "";
        if (pos < count && tk[pos] == "[") {
            // Valve 220: [ ux uy uz uoff ] [ vx vy vz voff ] rot sx sy
            face.uvMode = FaceUV::Valve220;
            ++pos;
            face.uAxis = { ParseDouble(tk[pos]), ParseDouble(tk[pos + 1]), ParseDouble(tk[pos + 2]) };
            face.uOffset = ParseDouble(tk[pos + 3]);
            pos += 4;
            if (pos < count && tk[pos] == "]") ++pos;
            if (pos < count && tk[pos] == "[") ++pos;
            face.vAxis = { ParseDouble(tk[pos]), ParseDouble(tk[pos + 1]), ParseDouble(tk[pos + 2]) };
            face.vOffset = ParseDouble(tk[pos + 3]);
            pos += 4;
            if (pos < count && tk[pos] == "]") ++pos;
            face.rotation = ParseDouble(tk[pos]);
            face.scaleX = ParseDouble(tk[pos + 1]);
            face.scaleY = ParseDouble(tk[pos + 2]);
            pos += 3;
        } else {
            // Classic: xoff yoff rot sx sy
            face.uvMode = FaceUV::Classic;
            face.uOffset = ParseDouble(tk[pos]);
            face.vOffset = ParseDouble(tk[pos + 1]);
            face.rotation = ParseDouble(tk[pos + 2]);
            face.scaleX = ParseDouble(tk[pos + 3]);
            face.scaleY = ParseDouble(tk[pos + 4]);
            pos += 5;
        }
        if (face.scaleX == 0) face.scaleX = 1;
        if (face.scaleY == 0) face.scaleY = 1;
        return true;
    }

    // patchDef2 { TEX ( w h a b c ) ( (row)(row)... ) }
    bool ParsePatch(const std::vector<std::string>& tk, size_t& pos, MapPatch& patch) {
        const size_t count = tk.size();
        if (pos < count && tk[pos] == "{") ++pos;
        patch.texture = (pos < count) ? tk[pos++] : "";
        if (pos < count && tk[pos] == "(") ++pos;
        patch.width = static_cast<int>(ParseDouble(tk[pos]));
        patch.height = static_cast<int>(ParseDouble(tk[pos + 1]));
        pos += 2;
        while (pos < count && tk[pos] != ")")
            ++pos;// skip contents/flags/value
        if (pos < count) ++pos;// ')'
        if (pos < count && tk[pos] == "(") ++pos;// grid '('
        patch.points.reserve(static_cast<size_t>(patch.width) * patch.height);
        for (int col = 0; col < patch.width; ++col) {
            if (pos < count && tk[pos] == "(") ++pos;// column '('
            for (int row = 0; row < patch.height; ++row) {
                if (pos < count && tk[pos] == "(") ++pos;
                MapPatchVert v;
                v.pos = { ParseDouble(tk[pos]), ParseDouble(tk[pos + 1]), ParseDouble(tk[pos + 2]) };
                v.st = { ParseDouble(tk[pos + 3]), ParseDouble(tk[pos + 4]) };
                pos += 5;
                if (pos < count && tk[pos] == ")") ++pos;
                patch.points.push_back(v);
            }
            if (pos < count && tk[pos] == ")") ++pos;// column ')'
        }
        if (pos < count && tk[pos] == ")") ++pos;// grid ')'
        while (pos < count && tk[pos] != "}")
            ++pos;// tolerate trailing tokens
        if (pos < count) ++pos;// patchDef '}'
        return patch.width >= 3 && patch.height >= 3
               && patch.points.size() == static_cast<size_t>(patch.width) * patch.height;
    }

    // Parse the whole file into entities, preserving every key/value block.
    std::vector<MapEntity> ParseMap(const std::vector<std::string>& tokens) {
        std::vector<MapEntity> entities;
        size_t pos = 0;
        const size_t count = tokens.size();

        while (pos < count) {
            if (tokens[pos] != "{") {
                ++pos;
                continue;
            }
            ++pos;// entity '{'
            MapEntity entity;
            while (pos < count && tokens[pos] != "}") {
                if (tokens[pos] == "{") {
                    ++pos;// brush-block '{'
                    if (pos < count && tokens[pos] == "brushDef") {
                        ++pos;
                        if (pos < count && tokens[pos] == "{") ++pos;
                        MapBrush brush;
                        MapFace f;
                        while (pos < count && tokens[pos] == "(" && ParseFace(tokens, pos, f, true))
                            brush.faces.push_back(f);
                        if (pos < count && tokens[pos] == "}") ++pos;// brushDef '}'
                        if (pos < count && tokens[pos] == "}") ++pos;// block '}'
                        if (brush.faces.size() >= 4) entity.brushes.push_back(std::move(brush));
                    } else if (pos < count && (tokens[pos] == "patchDef2" || tokens[pos] == "patchDef3")) {
                        ++pos;
                        MapPatch patch;
                        if (ParsePatch(tokens, pos, patch)) entity.patches.push_back(std::move(patch));
                        if (pos < count && tokens[pos] == "}") ++pos;// block '}'
                    } else {
                        MapBrush brush;
                        MapFace f;
                        while (pos < count && tokens[pos] == "(" && ParseFace(tokens, pos, f, false))
                            brush.faces.push_back(f);
                        if (pos < count && tokens[pos] == "}") ++pos;// block '}'
                        if (brush.faces.size() >= 4) entity.brushes.push_back(std::move(brush));
                    }
                } else {
                    const std::string& key = tokens[pos];
                    const std::string value = (pos + 1 < count) ? tokens[pos + 1] : "";
                    if (key == "classname") entity.classname = value;
                    entity.properties[key] = value;
                    pos += (pos + 1 < count) ? 2 : 1;
                }
            }
            if (pos < count) ++pos;// entity '}'
            entities.push_back(std::move(entity));
        }
        return entities;
    }

    // ── Geometry building ────────────────────────────────────────────────────

    struct EntityGeometry {
        std::vector<MeshData> batches;
        std::vector<PrefabCollider> colliders;
    };

    // Convert one entity's brushes + patches into per-texture mesh batches and
    // per-brush convex colliders (engine space, scaled).
    EntityGeometry BuildEntityGeometry(const MapEntity& entity, float scale) {
        EntityGeometry out;

        auto batchFor = [&](const std::string& tex, bool texels) -> MeshData& {
            for (auto& b : out.batches)
                if (b.material == tex && b.uvInTexels == texels) return b;
            out.batches.push_back(MeshData{});
            out.batches.back().material = tex;
            out.batches.back().uvInTexels = texels;
            return out.batches.back();
        };

        for (const auto& brush : entity.brushes) {
            PrefabCollider collider;
            bool solid = false;
            for (const auto& f : brush.faces)
                if (!IsNonSolidTexture(f.texture)) solid = true;

            for (size_t fi = 0; fi < brush.faces.size(); ++fi) {
                const MapFace& face = brush.faces[fi];

                // Seed a large quad on this face's plane, oriented CCW around n.
                glm::dvec3 n = face.normal;
                glm::dvec3 up = (std::abs(n.z) < 0.999) ? glm::dvec3(0, 0, 1) : glm::dvec3(1, 0, 0);
                glm::dvec3 uDir = glm::normalize(glm::cross(up, n));
                glm::dvec3 vDir = glm::normalize(glm::cross(n, uDir));// cross(uDir, vDir) == n
                glm::dvec3 origin = n * face.dist;
                constexpr double kBig = 1.0e6;
                std::vector<glm::dvec3> poly = { origin - uDir * kBig - vDir * kBig,
                                                 origin + uDir * kBig - vDir * kBig,
                                                 origin + uDir * kBig + vDir * kBig,
                                                 origin - uDir * kBig + vDir * kBig };
                for (size_t fj = 0; fj < brush.faces.size() && poly.size() >= 3; ++fj) {
                    if (fj == fi) continue;
                    poly = ClipPolygon(poly, brush.faces[fj].normal, brush.faces[fj].dist);
                }
                if (poly.size() < 3) continue;// face fully clipped away

                if (solid)
                    for (const glm::dvec3& q : poly)
                        collider.points.push_back(scale * QuakeToEngine(q));

                if (IsNonRenderTexture(face.texture)) continue;

                // UV projection axes / matrix per face format.
                glm::dvec3 texU, texV;
                if (face.uvMode == FaceUV::Valve220) {
                    texU = face.uAxis;
                    texV = face.vAxis;
                } else if (face.uvMode == FaceUV::BrushPrimitive) {
                    ComputeAxisBase(n, texU, texV);
                } else {
                    int best = 0;
                    double bestDot = -1.0;
                    for (int i = 0; i < 6; ++i) {
                        double d = glm::dot(n, kBaseAxis[i * 3]);
                        if (d > bestDot) {
                            bestDot = d;
                            best = i;
                        }
                    }
                    texU = kBaseAxis[best * 3 + 1];
                    texV = kBaseAxis[best * 3 + 2];
                }

                const glm::vec3 engineNormal = glm::normalize(QuakeToEngine(n));
                glm::vec3 tangent = glm::normalize(glm::cross(engineNormal, glm::vec3(0, 1, 0)));
                if (glm::length(tangent) < 0.01f)
                    tangent = glm::normalize(glm::cross(engineNormal, glm::vec3(1, 0, 0)));
                glm::vec3 bitangent = glm::cross(engineNormal, tangent);

                const double cosR = std::cos(glm::radians(face.rotation));
                const double sinR = std::sin(glm::radians(face.rotation));

                // brushDef UVs are already normalized (its matrix folds in the
                // texture size); classic/Valve UVs are in texels — Instantiate
                // divides by the real texture size (fallback 64).
                const bool texels = face.uvMode != FaceUV::BrushPrimitive;
                MeshData& md = batchFor(face.texture, texels);
                const auto vertBase = static_cast<uint32_t>(md.vertices.size());
                if (vertBase + poly.size() > 65535) continue;// split later; be safe here too

                for (const glm::dvec3& q : poly) {
                    double u, v;
                    if (face.uvMode == FaceUV::BrushPrimitive) {
                        const double px = glm::dot(q, texU);
                        const double py = glm::dot(q, texV);
                        u = face.texMat[0][0] * px + face.texMat[0][1] * py + face.texMat[0][2];
                        v = face.texMat[1][0] * px + face.texMat[1][1] * py + face.texMat[1][2];
                    } else {
                        u = glm::dot(q, texU) / face.scaleX;
                        v = glm::dot(q, texV) / face.scaleY;
                        if (face.uvMode == FaceUV::Classic) {
                            const double ru = u * cosR - v * sinR;
                            const double rv = u * sinR + v * cosR;
                            u = ru;
                            v = rv;
                        }
                        u += face.uOffset;
                        v += face.vOffset;
                    }
                    Vertex vert;
                    vert.position = scale * QuakeToEngine(q);
                    vert.uv = glm::vec2(static_cast<float>(u), static_cast<float>(v));
                    vert.normal = engineNormal;
                    vert.tangent = tangent;
                    vert.bitangent = bitangent;
                    md.vertices.push_back(vert);
                }
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    md.indices.push_back(static_cast<uint16_t>(vertBase));
                    md.indices.push_back(static_cast<uint16_t>(vertBase + k));
                    md.indices.push_back(static_cast<uint16_t>(vertBase + k + 1));
                }
            }
            if (!collider.points.empty()) out.colliders.push_back(std::move(collider));
        }

        // ── Patches: tessellate biquadratic Bezier 3x3 sub-patches ───────────
        constexpr int kTess = 8;// subdivisions per 3x3 sub-patch edge
        for (const auto& patch : entity.patches) {
            if (IsNonRenderTexture(patch.texture)) continue;
            MeshData& md = batchFor(patch.texture, /*texels*/ false);// patch STs are normalized
            auto ctrl = [&](int col, int row) -> const MapPatchVert& {
                return patch.points[static_cast<size_t>(col) * patch.height + row];
            };
            auto bez = [](double a, double b, double c, double t) {
                const double it = 1.0 - t;
                return it * it * a + 2.0 * it * t * b + t * t * c;
            };
            for (int pc = 0; pc + 2 < patch.width; pc += 2) {
                for (int pr = 0; pr + 2 < patch.height; pr += 2) {
                    const auto vertBase = static_cast<uint32_t>(md.vertices.size());
                    if (vertBase + (kTess + 1) * (kTess + 1) > 65535) continue;
                    // Evaluate the (kTess+1)^2 grid.
                    for (int i = 0; i <= kTess; ++i) {
                        const double s = double(i) / kTess;
                        for (int j = 0; j <= kTess; ++j) {
                            const double t = double(j) / kTess;
                            glm::dvec3 rowP[3];
                            glm::dvec2 rowT[3];
                            for (int r = 0; r < 3; ++r) {
                                const auto &a = ctrl(pc + 0, pr + r), &b = ctrl(pc + 1, pr + r),
                                           &c = ctrl(pc + 2, pr + r);
                                rowP[r] = { bez(a.pos.x, b.pos.x, c.pos.x, s),
                                            bez(a.pos.y, b.pos.y, c.pos.y, s),
                                            bez(a.pos.z, b.pos.z, c.pos.z, s) };
                                rowT[r] = { bez(a.st.x, b.st.x, c.st.x, s), bez(a.st.y, b.st.y, c.st.y, s) };
                            }
                            glm::dvec3 P = { bez(rowP[0].x, rowP[1].x, rowP[2].x, t),
                                             bez(rowP[0].y, rowP[1].y, rowP[2].y, t),
                                             bez(rowP[0].z, rowP[1].z, rowP[2].z, t) };
                            glm::dvec2 T = { bez(rowT[0].x, rowT[1].x, rowT[2].x, t),
                                             bez(rowT[0].y, rowT[1].y, rowT[2].y, t) };
                            Vertex vert;
                            vert.position = scale * QuakeToEngine(P);
                            vert.uv = glm::vec2(static_cast<float>(T.x), static_cast<float>(T.y));
                            vert.normal = glm::vec3(0, 1, 0);// refined below
                            vert.tangent = glm::vec3(1, 0, 0);
                            vert.bitangent = glm::vec3(0, 0, 1);
                            md.vertices.push_back(vert);
                        }
                    }
                    // Indices + finite-difference normals.
                    auto at = [&](int i, int j) { return vertBase + i * (kTess + 1) + j; };
                    for (int i = 0; i < kTess; ++i)
                        for (int j = 0; j < kTess; ++j) {
                            md.indices.push_back(static_cast<uint16_t>(at(i, j)));
                            md.indices.push_back(static_cast<uint16_t>(at(i + 1, j)));
                            md.indices.push_back(static_cast<uint16_t>(at(i + 1, j + 1)));
                            md.indices.push_back(static_cast<uint16_t>(at(i, j)));
                            md.indices.push_back(static_cast<uint16_t>(at(i + 1, j + 1)));
                            md.indices.push_back(static_cast<uint16_t>(at(i, j + 1)));
                        }
                    for (int i = 0; i <= kTess; ++i)
                        for (int j = 0; j <= kTess; ++j) {
                            const int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, kTess);
                            const int j0 = std::max(j - 1, 0), j1 = std::min(j + 1, kTess);
                            const glm::vec3 du = md.vertices[at(i1, j)].position - md.vertices[at(i0, j)].position;
                            const glm::vec3 dv = md.vertices[at(i, j1)].position - md.vertices[at(i, j0)].position;
                            const glm::vec3 nrm = glm::cross(du, dv);
                            if (glm::length(nrm) > 1e-8f) md.vertices[at(i, j)].normal = glm::normalize(nrm);
                        }
                }
            }
        }

        return out;
    }

    glm::vec3 ParseOrigin(const MapEntity& e, float scale) {
        auto it = e.properties.find("origin");
        if (it == e.properties.end()) return glm::vec3(0.0f);
        std::istringstream ss(it->second);
        glm::dvec3 q(0);
        ss >> q.x >> q.y >> q.z;
        return scale * QuakeToEngine(q);
    }

}// namespace

Prefab ImportMapPrefab(const std::string& path, float scale) {
    // Read through FileSystem so `path` resolves against the executable dir
    // (SDL_GetBasePath), consistent with every other engine asset.
    const FileSystem::Bytes bytes = FileSystem::Get().ReadSync(path);
    if (bytes.empty()) return Prefab{};// ok = false; caller logs

    const std::string text(bytes.begin(), bytes.end());
    return ImportMapPrefabFromText(text, FileSystem::BaseName(path), scale);
}

Prefab ImportMapPrefabFromText(const std::string& text, const std::string& name, float scale) {
    Prefab prefab;
    const std::vector<MapEntity> entities = ParseMap(TokenizeMap(text));

    prefab.root.name = name;

    auto addGeometry = [&](PrefabNode& node, EntityGeometry&& geo) {
        for (MeshData& raw : geo.batches) {
            for (MeshData& md : SplitMeshData(raw)) {
                node.meshes.push_back(static_cast<int>(prefab.meshes.size()));
                prefab.meshes.push_back(std::move(md));
            }
        }
        for (PrefabCollider& c : geo.colliders) {
            node.colliders.push_back(static_cast<int>(prefab.colliders.size()));
            prefab.colliders.push_back(std::move(c));
        }
    };

    for (size_t i = 0; i < entities.size(); ++i) {
        const MapEntity& e = entities[i];
        // worldspawn is the level itself; func_group is a TrenchBroom
        // editor-only grouping — both merge into the prefab root.
        const bool isWorld = e.classname.empty() || e.classname == "worldspawn" || e.classname == "func_group";

        if (isWorld) {
            if (e.classname == "worldspawn") {
                prefab.root.classname = "worldspawn";
                for (const auto& kv : e.properties)
                    prefab.root.properties.insert(kv);
            }
            addGeometry(prefab.root, BuildEntityGeometry(e, scale));
            continue;
        }

        PrefabNode node;
        node.classname = e.classname;
        node.properties = e.properties;
        node.name = e.classname + "_" + std::to_string(i);

        if (!e.brushes.empty() || !e.patches.empty()) {
            // Brush entity: geometry is authored in absolute map coordinates, so
            // the node transform stays identity (an "origin" key on rotating
            // entities is metadata for the game, not a bake-in offset).
            addGeometry(node, BuildEntityGeometry(e, scale));
        } else {
            // Point entity: origin/angle become the node transform.
            glm::mat4 xf = glm::translate(glm::mat4(1.0f), ParseOrigin(e, scale));
            auto ang = e.properties.find("angle");
            if (ang != e.properties.end())
                xf = glm::rotate(xf, glm::radians(static_cast<float>(ParseDouble(ang->second))), glm::vec3(0, 1, 0));
            node.transform = xf;

            if (e.classname == "light") {
                PrefabLight light;
                light.type = PrefabLight::Type::Point;
                auto lv = e.properties.find("light");
                const float quakeIntensity =
                    lv != e.properties.end() ? static_cast<float>(ParseDouble(lv->second)) : 300.0f;
                light.intensity = quakeIntensity / 300.0f;// 300 = Quake nominal
                light.range = quakeIntensity * scale;
                auto cv = e.properties.find("_color");
                if (cv == e.properties.end()) cv = e.properties.find("color");
                if (cv != e.properties.end()) {
                    std::istringstream ss(cv->second);
                    glm::vec3 c(1.0f);
                    ss >> c.r >> c.g >> c.b;
                    if (c.r > 1.0f || c.g > 1.0f || c.b > 1.0f) c /= 255.0f;// 0..255 authoring
                    light.color = c;
                }
                node.lights.push_back(static_cast<int>(prefab.lights.size()));
                prefab.lights.push_back(light);
            }
        }
        prefab.root.children.push_back(std::move(node));
    }

    prefab.ok = !prefab.meshes.empty() || !prefab.colliders.empty() || !prefab.root.children.empty();
    return prefab;
}

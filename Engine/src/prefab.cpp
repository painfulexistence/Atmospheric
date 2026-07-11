#include "prefab.hpp"
#include "file_system.hpp"
#include <cctype>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

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

// ─────────────────────────────────────────────────────────────────────────────
// TrenchBroom / Quake ".map" brush-format importer (pure CPU → MeshData)
//
// A .map is a list of entities `{ … }`; a brush entity additionally contains
// brushes `{ … }`, each a set of face planes. A brush is the convex volume
// carved out by the intersection of every face's half-space, so a face's actual
// polygon is recovered by clipping a huge quad on that plane against all the
// other planes of the brush (the same approach id's qbsp uses).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

    struct MapFace {
        glm::dvec3 normal{ 0 };// outward plane normal (Quake space)
        double dist = 0;// plane offset: dot(normal, x) == dist on the plane
        std::string texture;
        bool valve220 = false;// explicit texture axes vs. classic base-axis derivation
        glm::dvec3 uAxis{ 1, 0, 0 }, vAxis{ 0, 1, 0 };
        double uOffset = 0, vOffset = 0;
        double rotation = 0, scaleX = 1, scaleY = 1;
    };

    struct MapBrush {
        std::vector<MapFace> faces;
    };

    struct MapEntity {
        std::string classname;
        std::vector<MapBrush> brushes;
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

    double ParseDouble(const std::string& s) {
        try {
            return std::stod(s);
        } catch (...) {
            return 0.0;
        }
    }

    // Parse the whole file into entities, preserving brush→entity grouping.
    std::vector<MapEntity> ParseMap(const std::vector<std::string>& tokens) {
        std::vector<MapEntity> entities;
        size_t pos = 0;
        const size_t count = tokens.size();

        while (pos < count) {
            if (tokens[pos] != "{") {// skip stray tokens
                ++pos;
                continue;
            }
            ++pos;// consume entity '{'
            MapEntity entity;
            while (pos < count && tokens[pos] != "}") {
                if (tokens[pos] == "{") {
                    ++pos;// consume brush '{'
                    MapBrush brush;
                    while (pos < count && tokens[pos] != "}") {
                        // Face: '(' x y z ')' '(' x y z ')' '(' x y z ')' TEX …
                        glm::dvec3 p[3];
                        bool ok = true;
                        for (auto& pt : p) {
                            if (pos >= count || tokens[pos] != "(") {
                                ok = false;
                                break;
                            }
                            ++pos;
                            pt.x = ParseDouble(tokens[pos < count ? pos : count - 1]);
                            pt.y = ParseDouble(tokens[pos + 1 < count ? pos + 1 : count - 1]);
                            pt.z = ParseDouble(tokens[pos + 2 < count ? pos + 2 : count - 1]);
                            pos += 3;
                            if (pos >= count || tokens[pos] != ")") {
                                ok = false;
                                break;
                            }
                            ++pos;// consume ')'
                        }
                        if (!ok) break;

                        MapFace face;
                        face.texture = (pos < count) ? tokens[pos++] : "";
                        face.normal = glm::normalize(glm::cross(p[0] - p[1], p[2] - p[1]));
                        face.dist = glm::dot(face.normal, p[0]);

                        if (pos < count && tokens[pos] == "[") {
                            // Valve 220: '[' ux uy uz uoff ']' '[' vx vy vz voff ']' rot sx sy
                            face.valve220 = true;
                            ++pos;// '['
                            face.uAxis = { ParseDouble(tokens[pos]),
                                           ParseDouble(tokens[pos + 1]),
                                           ParseDouble(tokens[pos + 2]) };
                            face.uOffset = ParseDouble(tokens[pos + 3]);
                            pos += 4;
                            if (pos < count && tokens[pos] == "]") ++pos;
                            if (pos < count && tokens[pos] == "[") ++pos;
                            face.vAxis = { ParseDouble(tokens[pos]),
                                           ParseDouble(tokens[pos + 1]),
                                           ParseDouble(tokens[pos + 2]) };
                            face.vOffset = ParseDouble(tokens[pos + 3]);
                            pos += 4;
                            if (pos < count && tokens[pos] == "]") ++pos;
                            face.rotation = ParseDouble(tokens[pos]);
                            face.scaleX = ParseDouble(tokens[pos + 1]);
                            face.scaleY = ParseDouble(tokens[pos + 2]);
                            pos += 3;
                        } else {
                            // Classic: xoff yoff rot sx sy
                            face.uOffset = ParseDouble(tokens[pos]);
                            face.vOffset = ParseDouble(tokens[pos + 1]);
                            face.rotation = ParseDouble(tokens[pos + 2]);
                            face.scaleX = ParseDouble(tokens[pos + 3]);
                            face.scaleY = ParseDouble(tokens[pos + 4]);
                            pos += 5;
                        }
                        if (face.scaleX == 0) face.scaleX = 1;
                        if (face.scaleY == 0) face.scaleY = 1;
                        brush.faces.push_back(face);
                    }
                    if (pos < count) ++pos;// consume brush '}'
                    if (brush.faces.size() >= 4) entity.brushes.push_back(std::move(brush));
                } else {
                    // key/value pair: capture classname, skip the rest.
                    const std::string& key = tokens[pos];
                    const std::string value = (pos + 1 < count) ? tokens[pos + 1] : "";
                    if (key == "classname") entity.classname = value;
                    pos += (pos + 1 < count) ? 2 : 1;
                }
            }
            if (pos < count) ++pos;// consume entity '}'
            if (!entity.brushes.empty()) entities.push_back(std::move(entity));
        }
        return entities;
    }

    // Convert one brush entity's brushes into one MeshData per distinct texture
    // (a per-material batch — material == the .map texture name). Verified
    // out-of-engine against real TrenchBroom output.
    std::vector<MeshData> BuildEntityMeshes(const std::vector<MapBrush>& brushes, float scale) {
        constexpr double kTexSize = 64.0;// classic Quake textures are 64² by default
        std::vector<MeshData> batches;

        // Insertion-ordered find-or-create by texture name (few per entity, so a
        // linear scan keeps output deterministic without a hash map).
        auto batchFor = [&](const std::string& tex) -> MeshData& {
            for (auto& b : batches)
                if (b.material == tex) return b;
            batches.push_back(MeshData{});
            batches.back().material = tex;
            return batches.back();
        };

        for (const auto& brush : brushes) {
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

                // Clip against every other plane in the brush.
                for (size_t fj = 0; fj < brush.faces.size() && poly.size() >= 3; ++fj) {
                    if (fj == fi) continue;
                    poly = ClipPolygon(poly, brush.faces[fj].normal, brush.faces[fj].dist);
                }
                if (poly.size() < 3) continue;// face fully clipped away

                // Texture axes (Quake space) for UV projection.
                glm::dvec3 texU, texV;
                if (face.valve220) {
                    texU = face.uAxis;
                    texV = face.vAxis;
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

                MeshData& md = batchFor(face.texture);
                const auto vertBase = static_cast<uint32_t>(md.vertices.size());
                if (vertBase + poly.size() > 65535) continue;// this material batch is full

                for (const glm::dvec3& q : poly) {
                    double u = glm::dot(q, texU) / face.scaleX;
                    double v = glm::dot(q, texV) / face.scaleY;
                    // Classic rotation is a 2D rotation in texture space; Valve 220
                    // bakes rotation into the axes already.
                    if (!face.valve220) {
                        double ru = u * cosR - v * sinR;
                        double rv = u * sinR + v * cosR;
                        u = ru;
                        v = rv;
                    }
                    u += face.uOffset;
                    v += face.vOffset;
                    Vertex vert;
                    vert.position = scale * QuakeToEngine(q);
                    vert.uv = glm::vec2(static_cast<float>(u / kTexSize), static_cast<float>(v / kTexSize));
                    vert.normal = engineNormal;
                    vert.tangent = tangent;
                    vert.bitangent = bitangent;
                    md.vertices.push_back(vert);
                }

                // Triangle fan (poly is convex and CCW around the outward normal).
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    md.indices.push_back(static_cast<uint16_t>(vertBase));
                    md.indices.push_back(static_cast<uint16_t>(vertBase + k));
                    md.indices.push_back(static_cast<uint16_t>(vertBase + k + 1));
                }
            }
        }

        // Drop batches whose faces were all clipped away.
        std::vector<MeshData> out;
        for (auto& b : batches)
            if (!b.vertices.empty()) out.push_back(std::move(b));
        return out;
    }

    std::string BaseName(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

}// namespace

Prefab ImportMapPrefab(const std::string& path, float scale) {
    // Read through FileSystem so `path` resolves against the executable dir
    // (SDL_GetBasePath), consistent with every other engine asset. A raw
    // std::ifstream would resolve against the process cwd and miss the file
    // whenever the two differ (the cause of "no brush geometry found").
    const FileSystem::Bytes bytes = FileSystem::Get().ReadSync(path);
    if (bytes.empty()) return Prefab{};// ok = false; caller logs

    const std::string text(bytes.begin(), bytes.end());
    return ImportMapPrefabFromText(text, BaseName(path), scale);
}

Prefab ImportMapPrefabFromText(const std::string& text, const std::string& name, float scale) {
    Prefab prefab;
    const std::vector<MapEntity> entities = ParseMap(TokenizeMap(text));

    prefab.root.name = name;
    for (size_t i = 0; i < entities.size(); ++i) {
        std::vector<MeshData> batches = BuildEntityMeshes(entities[i].brushes, scale);
        if (batches.empty()) continue;

        // worldspawn is the level itself, not a movable object — its per-texture
        // batches attach directly to the prefab root (no wrapper node). Other
        // brush entities (func_door, …) keep their own node so they can be
        // transformed as a unit. Instantiate turns each mesh into a leaf
        // GameObject, so batches become sibling drawables either way.
        const bool isWorld = entities[i].classname.empty() || entities[i].classname == "worldspawn";
        PrefabNode entityNode;
        PrefabNode& target = isWorld ? prefab.root : entityNode;
        if (!isWorld) entityNode.name = entities[i].classname;
        for (MeshData& md : batches) {
            target.meshes.push_back(static_cast<int>(prefab.meshes.size()));
            prefab.meshes.push_back(std::move(md));
        }
        if (!isWorld) prefab.root.children.push_back(std::move(entityNode));
    }

    prefab.ok = !prefab.meshes.empty();
    return prefab;
}

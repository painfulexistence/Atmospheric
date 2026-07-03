#pragma once
#include "globals.hpp"
#include "buffer.hpp"
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

enum class PolygonMode {
    Fill,
    Line,
    Point
};

enum class RenderQueue {
    Background = 1000,// Skybox, far background
    Opaque = 2000,// Normal opaque objects
    AlphaTest = 2450,// Objects with alpha testing (vegetation, etc.)
    Transparent = 3000,// Transparent objects (glass, particles, etc.)
    Overlay = 4000// UI, HUD, debug overlays
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
    bool cullFaceEnabled = true;
    PrimitiveTopology primitiveType = PrimitiveTopology::Triangles;
    PolygonMode polygonMode = PolygonMode::Fill;

    RenderQueue renderQueue = RenderQueue::Opaque;
    int renderQueueOffset = 0;// Fine-tune rendering order within queue

    int GetFinalRenderQueue() const {
        return static_cast<int>(renderQueue) + renderQueueOffset;
    }

    virtual ~Material() = default;

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
        cullFaceEnabled = props.cullFaceEnabled;
        primitiveType = props.primitiveType;
        polygonMode = props.polygonMode;
    }
};

class WaterMaterial : public Material {
public:
    float     waterLine       = 32.0f;
    float     waveStrength    =  0.1f;
    float     waveSpeed       =  1.0f;
    // Fog color is not stored here -- WaterPass reads it live from SkyboxPass::skyColor,
    // matching VX (chunk/water fog both track the sky gradient color every frame).
    float     waterFogDensity =  0.00001f; // VX: u_fog_density in scene.py render_water/render_terrain
    glm::vec3 deepColor       = {0.05f, 0.1f, 0.25f};   // VX COLOR_INDIGO
    glm::vec3 shallowColor    = {0.686f, 0.933f, 0.933f}; // VX COLOR_MINT_GREEN
    float     beerCoef        =  0.095f;

    WaterMaterial() : Material(MaterialProps{}) {
        renderQueue     = RenderQueue::Transparent;
        cullFaceEnabled = false;
    }
};

// One detail texture layer of a terrain, blended by splat-map weight (or by
// the automatic slope/height weights when no splat map is set).
struct TerrainLayer {
    TextureHandle albedoMap;
    TextureHandle normalMap;         // optional tangent-space detail normal
    float         tiling = 32.0f;    // texture repeats across the whole terrain
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
class TerrainMaterial : public Material {
public:
    static constexpr int MAX_LAYERS = 4;

    float heightScale        = 32.0f;
    float tessellationFactor = 16.0f;
    float worldSize          = 1024.0f;  // XZ extent; needed to derive normals from the heightmap

    TextureHandle splatMap;
    TerrainLayer  layers[MAX_LAYERS];
    int           layerCount = 0;

    TerrainMaterial() : Material(MaterialProps{}) {}
    explicit TerrainMaterial(const MaterialProps& props) : Material(props) {}
};
#pragma once
#include "shader.hpp"
#include <glm/vec3.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Raw material definition parsed from the scene "materials" object. Texture maps
// are kept as asset paths here (Phase 1 is pure — no GPU) and resolved to
// TextureHandles in LoadSceneResources (Phase 2). An empty path means "no map".
struct MaterialBlueprint {
    std::string name;
    glm::vec3 diffuse = glm::vec3(0.55f, 0.55f, 0.55f);
    glm::vec3 specular = glm::vec3(0.70f, 0.70f, 0.70f);
    glm::vec3 ambient = glm::vec3(0.00f, 0.00f, 0.00f);
    float shininess = 0.25f;
    bool cullFaceEnabled = true;
    std::string baseMap, normalMap, aoMap, roughnessMap, metallicMap, heightMap;

    // Transmission / volume / IOR — mirrors the glTF-imported fields on Material
    // (KHR_materials_transmission/_volume/_ior). Data only; no shader reads them
    // yet. Defaults = opaque thin-walled dielectric, so materials omitting these
    // keys are unaffected. transmissionMap/thicknessMap are asset paths (as the
    // other maps), resolved in LoadSceneResources.
    float transmissionFactor = 0.0f;
    float ior = 1.5f;
    float thicknessFactor = 0.0f;
    float attenuationDistance = std::numeric_limits<float>::infinity();
    glm::vec3 attenuationColor = glm::vec3(1.0f);
    std::string transmissionMap, thicknessMap;
};

// Resolved, instantiation-ready description of one entity (and its children).
// Built during Phase 1 (pure parse, no side effects) before any GameObjects are
// created.  The "prefab" key, when present, will be merged in Phase 1 in the
// future — Phase 2 sees only the final, merged resolvedData.
struct EntityBlueprint {
    nlohmann::json resolvedData;
};

// Full description of a scene: GPU resource lists + entity tree.
//
// Lifecycle:
//   Phase 1 (pure, can run on a job thread):
//     SceneBlueprint bp = ParseSceneBlueprint(jsonContent);
//   Phase 2 (main thread only):
//     LoadSceneResources(bp);   // upload textures / compile shaders
//     InstantiateScene(bp);     // create GameObjects + Components
//
// This separation is the prefab hook: Phase 1 will eventually resolve
// "prefab": "prefabs/foo.json" references — loading and merging the prefab
// file into the EntityBlueprint — so that Phase 2 never needs to touch the
// filesystem or parse JSON.
struct SceneBlueprint {
    std::string name;
    std::string rawJson;// preserved verbatim for AssetManager::StoreSceneJson

    // Resource declarations (loaded before entity instantiation)
    std::vector<std::string> textures;
    std::unordered_map<std::string, ShaderProgramProps> shaders;
    std::vector<MaterialBlueprint> materials;
    std::vector<std::string> meshes;// TODO: unload in UnloadSceneAssets

    // Top-level entity blueprints (children embedded in resolvedData["children"])
    std::vector<EntityBlueprint> entities;
};

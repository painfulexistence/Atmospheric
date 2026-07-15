#pragma once
#include "material.hpp"
#include "shader.hpp"
#include <glm/vec3.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Raw material definition parsed from the scene "materials" object. This is
// just MaterialProps (the shared construction input, and the single source of
// truth for the scalar params) plus the bits that can't live there: the shading
// model, and the texture *paths*. Paths stay as strings because Phase 1 is pure
// (off-thread, no GPU) and TextureHandle resolves to a loaded GPU texture on
// construction — LoadSceneResources (Phase 2) turns each *Path into the matching
// props.<map> handle. An empty path means "no map".
struct MaterialBlueprint {
    std::string name;
    // "pbr" (default, metallic/roughness) or "blinnphong" (legacy specular).
    std::string shading = "pbr";
    // Scalar surface params (diffuse, roughness/metallic factors, specular/
    // ambient/shininess, cull, …). The texture-handle members stay empty here;
    // Phase 2 fills them from the *Path strings below.
    MaterialProps props;
    std::string baseMapPath, normalMapPath, aoMapPath, roughnessMapPath, metallicMapPath, heightMapPath;

    // Transmission / volume / IOR — mirrors the glTF-imported fields on
    // PBRMaterial (KHR_materials_transmission/_volume/_ior). Data only; no shader
    // reads them yet. Defaults = opaque thin-walled dielectric, so materials
    // omitting these keys are unaffected. *Path are asset paths resolved in
    // LoadSceneResources.
    float transmissionFactor = 0.0f;
    float ior = 1.5f;
    float thicknessFactor = 0.0f;
    float attenuationDistance = std::numeric_limits<float>::infinity();
    glm::vec3 attenuationColor = glm::vec3(1.0f);
    std::string transmissionMapPath, thicknessMapPath;
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

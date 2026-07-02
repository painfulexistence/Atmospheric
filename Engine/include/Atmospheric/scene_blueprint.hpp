#pragma once
#include "shader.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::string rawJson; // preserved verbatim for AssetManager::StoreSceneJson

    // Resource declarations (loaded before entity instantiation)
    std::vector<std::string>                             textures;
    std::unordered_map<std::string, ShaderProgramProps>  shaders;
    std::vector<std::string>                             meshes; // TODO: unload in UnloadSceneAssets

    // Top-level entity blueprints (children embedded in resolvedData["children"])
    std::vector<EntityBlueprint> entities;
};

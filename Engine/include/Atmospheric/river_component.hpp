#pragma once
#include "component.hpp"
#include "globals.hpp"
#include "terrain_flow.hpp"
#include <glm/vec3.hpp>

class Material;
class StreamingTerrainComponent;

// Streams a hydrology-derived river network onto a StreamingTerrain. At attach
// it runs one global flow-accumulation pass over the terrain's exact height
// source (see TerrainFlow), meshes the resulting polylines into draped ribbons,
// and renders them with the flowing-water RiverMaterial. Visual-only (no
// collision/physics). Build once; the network is static for a given world.
//
//   // Carve first so the terrain has valleys, then drape the water into them:
//   auto hydro = BuildHydrology(base01, worldSize, heightScale, {}, {});
//   props.heightFn = hydro.carvedHeight01;               // terrain follows it
//   ... build the StreamingTerrain ...
//   auto* go = CreateGameObject(glm::vec3(0.0f));         // must be at the origin
//   go->AddComponent<RiverComponent>(terrainComp, hydro.rivers, RiverProps{});
// river mesh vertices are already world-space, so the owner transform stays I.
struct RiverProps {
    RiverNetworkParams network;// flow-accumulation / extraction tuning
    RiverMeshParams mesh;// ribbon draping / UV tuning
    glm::vec3 shallowColor = glm::vec3(0.28f, 0.52f, 0.55f);
    glm::vec3 deepColor = glm::vec3(0.05f, 0.16f, 0.24f);
    float flowSpeed = 0.35f;
    float rippleStrength = 0.35f;
    float glint = 0.6f;
    float alpha = 0.82f;
    // Aerial fog: leave <0 to inherit the terrain's fog so rivers recede with
    // the hills; set explicitly to override.
    float fogDensity = -1.0f;
    glm::vec3 fogColor = glm::vec3(0.62f, 0.71f, 0.85f);
};

class RiverComponent : public Component {
public:
    // `rivers` is the precomputed network (from BuildHydrology, so it matches
    // the carve the terrain was built with). Ribbons drape onto the terrain's
    // current — carved — height, so the water sits in the incised channels.
    RiverComponent(
        GameObject* owner,
        StreamingTerrainComponent* terrain,
        std::vector<RiverPolyline> rivers,
        const RiverProps& props = {}
    );

    std::string GetName() const override {
        return "River";
    }
    void OnAttach() override;
    void OnDetach() override {
    }

    int RiverCount() const {
        return _riverCount;
    }
    size_t TriangleCount() const {
        return _triCount;
    }

private:
    StreamingTerrainComponent* _terrain = nullptr;
    std::vector<RiverPolyline> _rivers;
    RiverProps _props;
    MeshHandle _mesh;
    Material* _material = nullptr;
    int _riverCount = 0;
    size_t _triCount = 0;
};

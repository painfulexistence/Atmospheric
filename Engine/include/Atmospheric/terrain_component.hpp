#pragma once
#include "component.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GraphicsServer;
class PhysicsServer;
class Mesh;
class Material;

struct TerrainProps {
    std::string heightmapPath;
    float worldSize = 100.0f;
    int resolution = 128;
    float heightScale = 32.0f;
    float minHeight = -64.0f;
    float maxHeight = 64.0f;
    Material* material = nullptr;
};

class TerrainComponent : public Component {
public:
    TerrainComponent(GameObject* owner, GraphicsServer* graphics, PhysicsServer* physics, const TerrainProps& props);
    ~TerrainComponent();

    std::string GetName() const override {
        return "TerrainComponent";
    }

    void OnAttach() override;
    void OnDetach() override;

    Mesh* GetMesh() const {
        return _mesh;
    }
    void SetMaterial(Material* material);

private:
    GraphicsServer* _graphics;
    PhysicsServer* _physics;
    Mesh* _mesh;
    std::vector<float> _heightData;// Keep heightmap data alive

    void LoadHeightmap(const std::string& path, const TerrainProps& props);
};

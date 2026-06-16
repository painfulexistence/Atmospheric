#include "terrain_component.hpp"
#include "asset_manager.hpp"
#include "bullet_collision.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "physics_server.hpp"

TerrainComponent::TerrainComponent(
  GameObject* owner, GraphicsServer* graphics, PhysicsServer* physics, const TerrainProps& props
)
  : Component(), _graphics(graphics), _physics(physics), _mesh(nullptr) {
    gameObject = owner;

    // Create terrain mesh
    _mesh = AssetManager::Get().CreateTerrainMesh("Terrain", props.worldSize, props.resolution);

    // Set material if provided
    if (props.material) {
        _mesh->SetMaterial(props.material);
    }

    // Load and process heightmap
    LoadHeightmap(props.heightmapPath, props);

    owner->AddComponent<MeshComponent>(_mesh);
}

TerrainComponent::~TerrainComponent() {
    // Mesh is managed by GraphicsServer, don't delete
}

void TerrainComponent::OnAttach() {
    // Register to graphics server if needed
    // For now, mesh is already registered when created
}

void TerrainComponent::OnDetach() {
    // Unregister from graphics server if needed
}

void TerrainComponent::SetMaterial(Material* material) {
    if (_mesh) {
        _mesh->SetMaterial(material);
    }
}

void TerrainComponent::LoadHeightmap(const std::string& path, const TerrainProps& props) {
    auto img = AssetManager::Get().LoadImage(path);
    if (!img) {
        throw std::runtime_error("Could not load heightmap: " + path);
    }

    // Process heightmap data
    const int terrainDataSize = img->width * img->height;
    _heightData.resize(terrainDataSize);

    for (int i = 0; i < terrainDataSize; ++i) {
        _heightData[i] = (static_cast<float>(img->byteArray[i]) / 255.0f) * props.heightScale;
    }

    // Create physics shape
    auto shape = new btHeightfieldTerrainShape(
      img->width, img->height, _heightData.data(), 1.0f, props.minHeight, props.maxHeight, 1, PHY_FLOAT, true
    );

    _mesh->SetShape(shape);
}

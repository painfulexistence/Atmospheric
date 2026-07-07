#pragma once
#include "component.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// A micro voxel volume attached to a GameObject. Owns the CPU-side voxel data
// (a dense palette-index grid + a coarse occupancy grid + a palette) and its
// procedural generation. MicroVoxelPass renders every attached volume by
// reading this data and uploading it to the GPU; the volume's world-space
// origin follows the GameObject's transform. Contrast with VoxelChunkComponent,
// which is the macro (greedy-meshed, triangle) voxel world.
class VoxelVolumeComponent : public Component {
public:
    explicit VoxelVolumeComponent(GameObject* owner, uint32_t seed = 1337u);

    std::string GetName() const override {
        return "VoxelVolume";
    }
    void OnAttach() override;// generate + register with MicroVoxelPass
    void OnDetach() override;// unregister
    void DrawImGui() override;

    // (Re)build the procedural demo volume (terrain + caves + ore + floating
    // crystals) on the CPU and flag it for GPU re-upload.
    void Generate(uint32_t seed);

    // World-space min corner. The GameObject position is the volume's
    // horizontal centre at its base: the grid is centred over the object in
    // x/z and rises from the object's y. (Stage 1 uses translation only;
    // rotation/scale come with the per-object raymarch stage.)
    glm::vec3 GetOrigin() const;
    float WorldExtent() const {
        return static_cast<float>(gridDim) * voxelSize;
    }

    // Grid config (gridDim must be a multiple of brickDim; Generate enforces it).
#ifdef __EMSCRIPTEN__
    int gridDim = 128;// 6.4 m at 5 cm voxels (WebGL2 fragment raymarch is pricier per pixel)
#else
    int gridDim = 256;// 12.8 m at 5 cm voxels
#endif
    int brickDim = 8;
    float voxelSize = 0.05f;// 5 cm voxels => 256^3 is a 12.8 m diorama
    uint32_t seed = 1337u;

    // CPU data, consumed by MicroVoxelPass for GPU upload.
    std::vector<uint8_t> volume;// gridDim^3 palette indices, 0 = air
    std::vector<uint8_t> occupancy;// (gridDim/brickDim)^3, nonzero = brick occupied
    std::vector<uint8_t> paletteRGBA;// 256 * 4, albedo per material index
    uint32_t solidCount = 0;
    bool dirty = true;// set by Generate, cleared by the pass after upload
};

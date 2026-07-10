#pragma once
#include "globals.hpp"// glad / GLES3 (GLuint, gl* entry points)
#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

class VoxelWorld;
class ShaderProgram;

// Voxel Cone Tracing (VCT) radiance grid for the greedy-meshed macro-voxel
// world. Because VoxelWorld already *is* a voxel grid, the expensive part of
// classic VCT — voxelizing triangle meshes — is free: we sample the world's
// 1 m voxels straight into a camera-centered RGBA16F 3D texture (rgb = injected
// radiance, a = opacity), mip it with glGenerateMipmap, and cone-trace it in
// voxel.frag. That keeps the whole path inside GL 4.1 (no imageStore/compute).
//
// Injection runs on the CPU when the world is edited or the camera has moved a
// full re-inject step, so a static sun costs nothing per frame. A single
// cascade (camera-centered cube) is enough for local color bleed / soft AO;
// cascaded clipmaps are a later increment.
class VoxelConeGI {
public:
    ~VoxelConeGI();

    // Rebuild the grid if the world changed or the camera crossed into a new
    // re-inject step. dim = cube resolution (also world extent in metres, since
    // VoxelWorld voxels are 1 m). Returns true if the grid is valid to bind.
    bool Update(
        const VoxelWorld& world,
        const glm::vec3& cameraPos,
        const glm::vec3& sunDir,
        const glm::vec3& sunColor,
        float sunStrength,
        const glm::vec3& ambient,
        int paletteIndex,
        bool worldDirty,
        int dim = 64
    );

    // Bind the radiance texture on texUnit and set the sampler + placement
    // uniforms (u_giRadiance, u_giOrigin, u_giCellSize, u_giDim, u_giMaxMip).
    void Bind(ShaderProgram* shader, int texUnit) const;

    bool Valid() const {
        return _tex != 0 && _valid;
    }
    glm::ivec3 Origin() const {
        return _origin;
    }
    int Dim() const {
        return _dim;
    }

private:
    void Inject(
        const VoxelWorld& world,
        const glm::ivec3& origin,
        const glm::vec3& sunDir,
        const glm::vec3& sunColor,
        float sunStrength,
        const glm::vec3& ambient,
        int paletteIndex
    );

    GLuint _tex = 0;
    int _dim = 0;
    int _maxMip = 0;
    glm::ivec3 _origin{ 0 };// world-space min corner (integer metres)
    bool _valid = false;
    bool _haveOrigin = false;
    // Re-inject when the camera moves at least this many cells from the grid
    // center, to bound CPU injection cost (one inject = dim^3 voxel samples).
    static constexpr int kReinjectStep = 8;
    std::vector<glm::vec4> _scratch;// reused CPU injection buffer (dim^3 texels)
};

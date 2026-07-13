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
// A full re-inject (dim^3 voxel samples + shadow walks + upload + mip) is far
// too much for one frame, so injection is AMORTIZED: each Update() processes a
// bounded number of z-slabs into a double-buffered back texture, uploading them
// as it goes. The front texture (last completed grid) keeps being sampled until
// the back finishes, then they swap. Nothing ever stalls the render thread, so
// there is no hitch on camera move or carve. A single camera-centered cascade;
// cascaded clipmaps are a later increment.
class VoxelConeGI {
public:
    ~VoxelConeGI();

    // Advance injection: (re)start a build when the world changed, the camera
    // crossed a re-inject step, or the resolution changed; otherwise push the
    // in-flight build forward by a few slabs. dim = cube resolution (also world
    // extent in metres, since VoxelWorld voxels are 1 m). Returns true if a
    // completed grid is available to bind (the front texture).
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

    // Bind the (completed) front radiance texture on texUnit and set the sampler
    // + placement uniforms (u_giRadiance, u_giOrigin, u_giCellSize, u_giDim,
    // u_giMaxMip).
    void Bind(ShaderProgram* shader, int texUnit) const;

    bool Valid() const {
        return _texFront != 0 && _valid;
    }

    // Slabs injected per Update() while a build is in flight. Lower = smoother
    // (more frames to converge), higher = the grid refreshes sooner.
    int slabsPerFrame = 4;

private:
    void StartBuild(const glm::ivec3& origin, int dim);
    void EnsureTexture(GLuint& tex, int dim) const;
    // Fill one z-slab of the back texture from the live world + captured lights.
    void InjectSlab(const VoxelWorld& world, int z);

    // Committed (front) grid — what Bind() exposes.
    GLuint _texFront = 0;
    int _dim = 0;
    int _maxMip = 0;
    glm::ivec3 _origin{ 0 };// world-space min corner (integer metres)
    bool _valid = false;

    // In-flight (back) build.
    GLuint _texBack = 0;
    bool _building = false;
    // Latched world-edit flag: a carve during an in-flight build is remembered
    // and honored by a fresh build once the current one completes, so builds
    // always run to completion (no restart thrash) yet never miss an edit.
    bool _pendingDirty = false;
    glm::ivec3 _injOrigin{ 0 };
    int _injDim = 0;
    int _injZ = 0;// next z-slab to process
    // Lighting captured when the build started, so the grid is internally
    // consistent even if the sun shifts mid-build.
    glm::vec3 _injSunDir{ 0.0f };
    glm::vec3 _injSunColor{ 1.0f };
    glm::vec3 _injAmbient{ 0.0f };
    float _injSunStrength = 1.0f;
    int _injPalette = 4;

    std::vector<glm::vec4> _slab;// reused CPU buffer for one z-slab (dim*dim)

    // Re-inject when the camera moves at least this many cells from the grid
    // center. A short shadow walk length keeps per-cell cost down.
    static constexpr int kReinjectStep = 8;
    static constexpr int kShadowSteps = 4;
};

#include "voxel_gi.hpp"
#include "shader.hpp"
#include "voxel_world.hpp"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <utility>// std::swap

// Cosine palette, kept in sync with voxel.frag's palette(): a + b*cos(2π(c*t+d)).
// Injection colors must match what the rasterizer shades so bounced light reads
// as the same material.
static glm::vec3 PaletteColor(int paletteIndex, float t) {
    glm::vec3 a, b, c, d;
    switch (paletteIndex) {
    case 0:
        a = { 0.800f, 0.150f, 0.560f };
        b = { 0.610f, 0.300f, 0.120f };
        c = { 0.640f, 0.100f, 0.590f };
        d = { 0.380f, 0.860f, 0.470f };
        break;
    case 1:
        a = { 0.288f, 0.303f, 0.466f };
        b = { 0.806f, 0.664f, 0.998f };
        c = { 1.253f, 0.992f, 1.569f };
        d = { 3.379f, 3.574f, 3.026f };
        break;
    case 2:
        a = { 0.420f, 0.696f, 0.625f };
        b = { 0.791f, 0.182f, 0.271f };
        c = { 0.368f, 0.650f, 0.103f };
        d = { 0.913f, 3.624f, 0.320f };
        break;
    case 3:
        a = { 0.427f, 0.346f, 0.372f };
        b = { 0.288f, 0.918f, 0.336f };
        c = { 0.635f, 1.136f, 0.404f };
        d = { 1.893f, 0.663f, 1.910f };
        break;
    case 5:
        a = { 0.686f, 0.933f, 0.933f };
        b = { 0.957f, 0.643f, 0.957f };
        c = { 0.867f, 0.627f, 0.867f };
        d = { 1.961f, 2.871f, 1.702f };
        break;
    default:// 4 — VX default
        a = { 0.746f, 0.815f, 0.846f };
        b = { 0.195f, 0.283f, 0.187f };
        c = { 1.093f, 1.417f, 1.405f };
        d = { 5.435f, 2.400f, 5.741f };
        break;
    }
    const float k = 6.28318f;
    return a
           + b * glm::vec3(std::cos(k * (c.x * t + d.x)), std::cos(k * (c.y * t + d.y)), std::cos(k * (c.z * t + d.z)));
}

VoxelConeGI::~VoxelConeGI() {
    if (_texFront) glDeleteTextures(1, &_texFront);
    if (_texBack) glDeleteTextures(1, &_texBack);
}

void VoxelConeGI::EnsureTexture(GLuint& tex, int dim) const {
    if (tex == 0) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    // Orphan/allocate storage (contents filled slab by slab via glTexSubImage3D).
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, dim, dim, dim, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void VoxelConeGI::StartBuild(const glm::ivec3& origin, int dim) {
    _injOrigin = origin;
    _injDim = dim;
    _injZ = 0;
    _building = true;
    _slab.assign(static_cast<size_t>(dim) * dim, glm::vec4(0.0f));
    EnsureTexture(_texBack, dim);
}

bool VoxelConeGI::Update(
    const VoxelWorld& world,
    const glm::vec3& cameraPos,
    const glm::vec3& sunDir,
    const glm::vec3& sunColor,
    float sunStrength,
    const glm::vec3& ambient,
    int paletteIndex,
    bool worldDirty,
    int dim
) {
    dim = std::clamp(dim, 16, 256);

    // Center the cascade on the camera, snapped to integer metres (== voxel
    // size), so injection maps 1:1 onto world voxels with no resampling.
    glm::ivec3 cam(
        static_cast<int>(std::floor(cameraPos.x)),
        static_cast<int>(std::floor(cameraPos.y)),
        static_cast<int>(std::floor(cameraPos.z))
    );
    glm::ivec3 desired = cam - glm::ivec3(dim / 2);

    // Latch edits so one arriving mid-build isn't lost; honored by the next
    // build (below) once the current one finishes.
    _pendingDirty = _pendingDirty || worldDirty;

    // Start a build only when idle — never restart an in-flight one, so builds
    // always complete and progress even under continuous carving / fast flight
    // (the grid then trails the latest state by at most one build cycle).
    if (!_building) {
        const glm::ivec3 delta = glm::abs(desired - _origin);
        const int cheby = std::max(delta.x, std::max(delta.y, delta.z));
        const bool stale = !_valid || _pendingDirty || dim != _dim || cheby >= kReinjectStep;
        if (stale) {
            // Capture lighting for a consistent build even if the sun shifts.
            _injSunDir = glm::normalize(sunDir);
            _injSunColor = sunColor;
            _injSunStrength = sunStrength;
            _injAmbient = ambient;
            _injPalette = paletteIndex;
            _pendingDirty = false;
            StartBuild(desired, dim);
        }
    }

    if (_building) {
        for (int n = 0; n < slabsPerFrame && _injZ < _injDim; ++n) {
            InjectSlab(world, _injZ);
            ++_injZ;
        }
        if (_injZ >= _injDim) {
            // Build complete: mip the back texture and swap it to the front.
            glBindTexture(GL_TEXTURE_3D, _texBack);
            glGenerateMipmap(GL_TEXTURE_3D);
            glBindTexture(GL_TEXTURE_3D, 0);
            std::swap(_texFront, _texBack);
            _origin = _injOrigin;
            _dim = _injDim;
            _maxMip = static_cast<int>(std::floor(std::log2(static_cast<float>(_injDim))));
            _valid = true;
            _building = false;
        }
    }

    return Valid();
}

void VoxelConeGI::InjectSlab(const VoxelWorld& world, int z) {
    const int N = _injDim;
    auto solid = [&](int wx, int wy, int wz) { return world.GetVoxel(wx, wy, wz) != 0; };

    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const int wx = _injOrigin.x + x, wy = _injOrigin.y + y, wz = _injOrigin.z + z;
            const size_t idx = static_cast<size_t>(y) * N + x;
            const uint8_t id = world.GetVoxel(wx, wy, wz);
            if (id == 0) {
                _slab[idx] = glm::vec4(0.0f);// air: transparent, no radiance
                continue;
            }
            const glm::vec3 albedo = PaletteColor(_injPalette, static_cast<float>(id) / 50.0f);

            // Shadow: march a few voxels toward the sun; blocked => shadowed.
            float sunVis = 1.0f;
            for (int s = 1; s <= kShadowSteps; ++s) {
                const glm::vec3 p = glm::vec3(wx, wy, wz) + _injSunDir * static_cast<float>(s) + glm::vec3(0.5f);
                if (solid(
                        static_cast<int>(std::floor(p.x)),
                        static_cast<int>(std::floor(p.y)),
                        static_cast<int>(std::floor(p.z))
                    )) {
                    sunVis = 0.0f;
                    break;
                }
            }
            // Sky access from directly above adds a soft ambient fill, so
            // exposed tops glow and enclosed voxels stay dark.
            const float skyAccess = solid(wx, wy + 1, wz) ? 0.0f : 1.0f;

            const glm::vec3 radiance =
                albedo * (_injSunColor * (_injSunStrength * sunVis) + _injAmbient * (0.5f + 0.5f * skyAccess));
            _slab[idx] = glm::vec4(radiance, 1.0f);// opacity 1 for solid
        }
    }

    glBindTexture(GL_TEXTURE_3D, _texBack);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, z, N, N, 1, GL_RGBA, GL_FLOAT, _slab.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}

void VoxelConeGI::Bind(ShaderProgram* shader, int texUnit) const {
    glActiveTexture(GL_TEXTURE0 + texUnit);
    glBindTexture(GL_TEXTURE_3D, _texFront);
    shader->SetUniform(std::string("u_giRadiance"), texUnit);
    shader->SetUniform(std::string("u_giOrigin"), glm::vec3(_origin));
    shader->SetUniform(std::string("u_giCellSize"), 1.0f);// 1 m voxels
    shader->SetUniform(std::string("u_giDim"), _dim);
    shader->SetUniform(std::string("u_giMaxMip"), static_cast<float>(_maxMip));
}

#include "voxel_gi.hpp"
#include "shader.hpp"
#include "voxel_world.hpp"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

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
    if (_tex) glDeleteTextures(1, &_tex);
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

    const glm::ivec3 delta = glm::abs(desired - _origin);
    const int cheby = std::max(delta.x, std::max(delta.y, delta.z));
    const bool needReinject = !_haveOrigin || worldDirty || _dim != dim || cheby >= kReinjectStep || _tex == 0;
    if (!needReinject) return Valid();

    _dim = dim;
    _origin = desired;
    _haveOrigin = true;
    Inject(world, desired, sunDir, sunColor, sunStrength, ambient, paletteIndex);
    return Valid();
}

void VoxelConeGI::Inject(
    const VoxelWorld& world,
    const glm::ivec3& origin,
    const glm::vec3& sunDir,
    const glm::vec3& sunColor,
    float sunStrength,
    const glm::vec3& ambient,
    int paletteIndex
) {
    const int N = _dim;
    _scratch.assign(static_cast<size_t>(N) * N * N, glm::vec4(0.0f));

    // One short shadow walk toward the sun approximates direct visibility
    // cheaply (full per-voxel shadow rays would be far too costly on the CPU):
    // step a few voxels along the sun direction and darken if anything blocks.
    const glm::vec3 sd = glm::normalize(sunDir);
    const int kShadowSteps = 6;

    auto solid = [&](int wx, int wy, int wz) { return world.GetVoxel(wx, wy, wz) != 0; };

    for (int z = 0; z < N; ++z) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const int wx = origin.x + x, wy = origin.y + y, wz = origin.z + z;
                const uint8_t id = world.GetVoxel(wx, wy, wz);
                const size_t idx = (static_cast<size_t>(z) * N + y) * N + x;
                if (id == 0) {
                    _scratch[idx] = glm::vec4(0.0f);// air: transparent, no radiance
                    continue;
                }

                const glm::vec3 albedo = PaletteColor(paletteIndex, static_cast<float>(id) / 50.0f);

                // Shadow: march a few voxels toward the sun; blocked => shadowed.
                float sunVis = 1.0f;
                for (int s = 1; s <= kShadowSteps; ++s) {
                    const glm::vec3 p = glm::vec3(wx, wy, wz) + sd * static_cast<float>(s) + glm::vec3(0.5f);
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
                    albedo * (sunColor * (sunStrength * sunVis) + ambient * (0.5f + 0.5f * skyAccess));
                _scratch[idx] = glm::vec4(radiance, 1.0f);// opacity 1 for solid
            }
        }
    }

    if (_tex == 0) glGenTextures(1, &_tex);
    glBindTexture(GL_TEXTURE_3D, _tex);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, N, N, N, 0, GL_RGBA, GL_FLOAT, _scratch.data());
    glGenerateMipmap(GL_TEXTURE_3D);
    glBindTexture(GL_TEXTURE_3D, 0);

    _maxMip = static_cast<int>(std::floor(std::log2(static_cast<float>(N))));
    _valid = true;
}

void VoxelConeGI::Bind(ShaderProgram* shader, int texUnit) const {
    glActiveTexture(GL_TEXTURE0 + texUnit);
    glBindTexture(GL_TEXTURE_3D, _tex);
    shader->SetUniform(std::string("u_giRadiance"), texUnit);
    shader->SetUniform(std::string("u_giOrigin"), glm::vec3(_origin));
    shader->SetUniform(std::string("u_giCellSize"), 1.0f);// 1 m voxels
    shader->SetUniform(std::string("u_giDim"), _dim);
    shader->SetUniform(std::string("u_giMaxMip"), static_cast<float>(_maxMip));
}

#include "voxel_volume_component.hpp"

#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "job_system.hpp"
#include "renderer.hpp"

#include <cmath>
#include <fmt/format.h>
#include <imgui.h>

// ============================================================================
//  Procedural demo volume generation (ported from project-vapor's Metal
//  renderer; reference-validated DDA/generator). Lives here so the volume data
//  is owned by the component; MicroVoxelPass only renders it.
// ============================================================================

static float MvHashNoise(int x, int y, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
                 + static_cast<uint32_t>(z) * 2246822519u + seed * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

static float MvValueNoise3(glm::vec3 p, uint32_t seed) {
    glm::vec3 pf = glm::floor(p);
    glm::vec3 f = p - pf;
    f = f * f * (3.0f - 2.0f * f);// smoothstep
    int xi = static_cast<int>(pf.x), yi = static_cast<int>(pf.y), zi = static_cast<int>(pf.z);

    auto corner = [&](int dx, int dy, int dz) { return MvHashNoise(xi + dx, yi + dy, zi + dz, seed); };
    float c000 = corner(0, 0, 0), c100 = corner(1, 0, 0), c010 = corner(0, 1, 0), c110 = corner(1, 1, 0);
    float c001 = corner(0, 0, 1), c101 = corner(1, 0, 1), c011 = corner(0, 1, 1), c111 = corner(1, 1, 1);

    float x00 = glm::mix(c000, c100, f.x), x10 = glm::mix(c010, c110, f.x);
    float x01 = glm::mix(c001, c101, f.x), x11 = glm::mix(c011, c111, f.x);
    return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
}

static float MvFbm3(glm::vec3 p, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * MvValueNoise3(p, seed + static_cast<uint32_t>(i) * 101u);
        p *= 2.0f;
        amp *= 0.5f;
    }
    return sum;// ~[0, 1)
}

// Gradient (Perlin-style) noise for the terrain heightfield — value noise has
// axis-aligned plateaus that read as fake terrain; hashed unit gradients with
// a quintic fade give the classic smooth rolling look.
static float MvGradDot(int xi, int yi, int zi, glm::vec3 offset, uint32_t seed) {
    glm::vec3 g(
        MvHashNoise(xi, yi, zi, seed) - 0.5f,
        MvHashNoise(xi, yi, zi, seed ^ 0x9E3779B9u) - 0.5f,
        MvHashNoise(xi, yi, zi, seed ^ 0x85EBCA6Bu) - 0.5f
    );
    float len = glm::length(g);
    if (len < 1e-6f) return offset.x;
    return glm::dot(g / len, offset);
}

static float MvGradNoise3(glm::vec3 p, uint32_t seed) {
    glm::vec3 pf = glm::floor(p);
    glm::vec3 f = p - pf;
    glm::vec3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);// quintic fade
    int xi = static_cast<int>(pf.x), yi = static_cast<int>(pf.y), zi = static_cast<int>(pf.z);

    auto corner = [&](int dx, int dy, int dz) {
        return MvGradDot(xi + dx, yi + dy, zi + dz, f - glm::vec3(dx, dy, dz), seed);
    };
    float x00 = glm::mix(corner(0, 0, 0), corner(1, 0, 0), u.x);
    float x10 = glm::mix(corner(0, 1, 0), corner(1, 1, 0), u.x);
    float x01 = glm::mix(corner(0, 0, 1), corner(1, 0, 1), u.x);
    float x11 = glm::mix(corner(0, 1, 1), corner(1, 1, 1), u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);// ~[-0.7, 0.7]
}

static float MvGradFbm01(glm::vec3 p, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * MvGradNoise3(p, seed + static_cast<uint32_t>(i) * 101u);
        p *= 2.0f;
        amp *= 0.5f;
    }
    return glm::clamp(0.5f + sum * 1.2f, 0.0f, 1.0f);
}

// ============================================================================

VoxelVolumeComponent::VoxelVolumeComponent(GameObject* owner, uint32_t seedIn) : seed(seedIn) {
    gameObject = owner;
}

glm::vec3 VoxelVolumeComponent::GetOrigin() const {
    float half = WorldExtent() * 0.5f;
    glm::vec3 pos = gameObject ? gameObject->GetPosition() : glm::vec3(0.0f);
    return glm::vec3(pos.x - half, pos.y, pos.z - half);
}

void VoxelVolumeComponent::Generate(uint32_t seedIn) {
    seed = seedIn;
    gridDim -= gridDim % brickDim;// Guard against non-brick-aligned sizes
    const uint32_t N = static_cast<uint32_t>(gridDim);
    const uint32_t B = static_cast<uint32_t>(brickDim);
    const uint32_t BG = N / B;

    volume.assign(static_cast<size_t>(N) * N * N, 0);
    occupancy.assign(static_cast<size_t>(BG) * BG * BG, 0);
    auto voxelAt = [this, N](uint32_t x, uint32_t y, uint32_t z) -> uint8_t& {
        return volume[(static_cast<size_t>(z) * N + y) * N + x];
    };

    // Material palette (index 0 = air). The alpha channel is repurposed as
    // per-material emission strength (0 = not emissive): voxels are always
    // opaque, so alpha is free, and the shaders read it as self-illumination
    // that the GI bounce also picks up (emissive voxels light their neighbors).
    enum : uint8_t {
        MatGrass = 1,
        MatDirt = 2,
        MatStone = 3,
        MatSnow = 4,
        MatSand = 5,
        MatOre = 6,
        MatCrystal = 7,
        MatGlow = 8,
        MatWater = 9
    };
    // 2-row material table (256 x 2 RGBA8), uploaded as a 2-row 2D texture:
    //   row 0: albedo.rgb, emission.a         (surface color + self-illumination)
    //   row 1: reflectivity.r, roughness.g,   (glossy-jittered reflection)
    //          transmission.b, ior.a          (dielectric BTDF; actual IOR
    //                                          decodes as 1.0 + a/255)
    // The shader reads row 0 with texelFetch(u_palette, ivec2(mat, 0)) and the
    // material params with ivec2(mat, 1).
    paletteRGBA.assign(256 * 2 * 4, 0);
    auto setPalette = [this](uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t emission = 0) {
        paletteRGBA[idx * 4 + 0] = r;
        paletteRGBA[idx * 4 + 1] = g;
        paletteRGBA[idx * 4 + 2] = b;
        paletteRGBA[idx * 4 + 3] = emission;
    };
    auto setMaterial = [this](uint8_t idx, uint8_t reflectivity, uint8_t roughness = 0) {
        const int base = (256 + idx) * 4;// row 1
        paletteRGBA[base + 0] = reflectivity;
        paletteRGBA[base + 1] = roughness;
    };
    // Dielectric transmission (the BTDF path): iorByte encodes actual IOR as
    // 1.0 + iorByte/255 (glass 1.5 -> 128, crystal 1.55 -> 140).
    auto setGlass = [this](uint8_t idx, uint8_t transmission, uint8_t iorByte) {
        const int base = (256 + idx) * 4;// row 1
        paletteRGBA[base + 2] = transmission;
        paletteRGBA[base + 3] = iorByte;
    };
    for (int i = 10; i < 256; i++)
        setPalette(static_cast<uint8_t>(i), 128, 128, 128);
    setPalette(MatGrass, 64, 140, 46);
    setPalette(MatDirt, 107, 77, 46);
    setPalette(MatStone, 122, 122, 128);
    setPalette(MatSnow, 235, 240, 250);
    setPalette(MatSand, 204, 184, 122);
    setPalette(MatOre, 242, 191, 64);
    setPalette(MatCrystal, 115, 191, 242, 160);// cool cyan glow (the original look)
    setPalette(MatGlow, 255, 140, 48, 255);// warm glowstone, full emission
    setPalette(MatWater, 56, 130, 196);// deep blue; Beer tints what's below
    // Reflective materials: crystals keep the original near-mirror; ore and
    // snow catch a roughness-jittered sheen. Everything else stays matte.
    setMaterial(MatCrystal, 210, 20);// glossy crystal — mirrors the scene
    setMaterial(MatOre, 90, 60);// metallic-ish speckle
    setMaterial(MatSnow, 40, 120);// faint wet sheen
    // Water is the transmission showcase: IOR 1.33 (byte 84), lightly rippled
    // refraction via the glossy jitter (roughness 25). Fresnel comes from the
    // IOR on the glass path, so reflectivity stays 0.
    setMaterial(MatWater, 0, 25);
    setGlass(MatWater, 215, 84);

    // Terrain heightfield: gradient-noise fbm with gentle amplitude (~0.28
    // height/width ratio) so the terrain reads as rolling hills rather than
    // 1:1-aspect spikes.
    const float baseH = 0.10f * static_cast<float>(N);
    const float varH = 0.34f * static_cast<float>(N);
    const float snowLine = baseH + 0.75f * varH;
    const float sandLine = baseH + 0.12f * varH;
    // Water fills valleys up to just under the sand line, so beaches ring it.
    const uint32_t waterLevel = static_cast<uint32_t>(baseH + 0.08f * varH);
    std::vector<float> heights(static_cast<size_t>(N) * N);
    for (uint32_t z = 0; z < N; z++) {
        for (uint32_t x = 0; x < N; x++) {
            float h01 = MvGradFbm01(glm::vec3(x, 0.0f, z) * 0.010f, 5, seed);
            h01 = std::pow(h01, 1.6f);// Bias toward valleys with occasional peaks
            heights[z * N + x] = baseH + h01 * varH;
        }
    }

    // Column fill, parallelized over z slices (cells are disjoint per worker)
    // via the engine JobSystem. The JobSystem owns a persistent worker pool, so
    // this is safe on the web: with Emscripten pthreads the workers already
    // exist (no spawn-during-join deadlock), and without pthreads Execute runs
    // the job inline on the main thread.
    const uint32_t workerCount = std::max(1u, JobSystem::Get()->GetThreadCount());
    std::vector<uint32_t> sliceSolidCounts(workerCount, 0);
    auto fillSlices = [&](uint32_t ti) {
        uint32_t localSolid = 0;
        for (uint32_t z = ti; z < N; z += workerCount) {
            for (uint32_t x = 0; x < N; x++) {
                const float h = heights[z * N + x];
                const auto top = static_cast<uint32_t>(h);
                for (uint32_t y = 0; y <= top && y < N; y++) {
                    // Carve caves below the surface crust
                    if (y + 4 < top) {
                        float cave = MvFbm3(glm::vec3(x, y, z) * 0.045f, 3, seed + 7u);
                        if (cave > 0.62f) continue;
                    }
                    uint8_t mat = MatStone;
                    const uint32_t depth = top - y;
                    if (depth == 0) {
                        mat = (h > snowLine) ? MatSnow : ((h < sandLine) ? MatSand : MatGrass);
                    } else if (depth <= 3) {
                        mat = (h < sandLine) ? MatSand : MatDirt;
                    } else if (MvHashNoise(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), seed + 13u)
                               > 0.995f) {
                        mat = MatOre;
                    }
                    voxelAt(x, y, z) = mat;
                    localSolid++;
                }
                // Water column: from the terrain surface up to the water level.
                // Water voxels are solid to the DDA; the shader's transmission
                // path refracts through them (Beer-tinted) to the bed below.
                for (uint32_t y = top + 1; y <= waterLevel && y < N; y++) {
                    voxelAt(x, y, z) = MatWater;
                    localSolid++;
                }
            }
        }
        sliceSolidCounts[ti] = localSolid;
    };
    for (uint32_t ti = 0; ti < workerCount; ti++) {
        JobSystem::Get()->Execute([&fillSlices, ti](int) { fillSlices(ti); });
    }
    JobSystem::Get()->Wait();
    solidCount = 0;
    for (uint32_t c : sliceSolidCounts)
        solidCount += c;

    // A few floating crystal spheres to show the volume is truly 3D
    for (int s = 0; s < 4; s++) {
        glm::vec3 center(
            (0.15f + 0.7f * MvHashNoise(s, 1, 0, seed + 23u)) * N,
            (0.70f + 0.22f * MvHashNoise(s, 2, 0, seed + 23u)) * N,
            (0.15f + 0.7f * MvHashNoise(s, 3, 0, seed + 23u)) * N
        );
        float radius = (0.02f + 0.03f * MvHashNoise(s, 4, 0, seed + 23u)) * N;
        int r = static_cast<int>(radius) + 1;
        for (int dz = -r; dz <= r; dz++) {
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (glm::length(glm::vec3(dx, dy, dz)) > radius) continue;
                    int x = static_cast<int>(center.x) + dx;
                    int y = static_cast<int>(center.y) + dy;
                    int z = static_cast<int>(center.z) + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(N) || y >= static_cast<int>(N)
                        || z >= static_cast<int>(N))
                        continue;
                    uint8_t& v = voxelAt(x, y, z);
                    if (v == 0) solidCount++;
                    v = MatCrystal;
                }
            }
        }
    }

    // A handful of warm glowstone orbs resting on the terrain surface. These
    // are emissive (full alpha), so the GI bounce ray picks them up and washes
    // the surrounding stone/dirt with warm indirect light — the signature
    // voxel-GI effect (glowing blocks lighting their neighbors), nearly free
    // because the bounce rays are already being traced.
    for (int g = 0; g < 6; g++) {
        int gx = static_cast<int>((0.18f + 0.64f * MvHashNoise(g, 5, 0, seed + 31u)) * N);
        int gz = static_cast<int>((0.18f + 0.64f * MvHashNoise(g, 6, 0, seed + 31u)) * N);
        if (gx < 0 || gz < 0 || gx >= static_cast<int>(N) || gz >= static_cast<int>(N)) continue;
        int gy = static_cast<int>(heights[static_cast<size_t>(gz) * N + gx]) + 1;// sit just above the surface
        const int rad = 2;
        for (int dz = -rad; dz <= rad; dz++) {
            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    if (dx * dx + dy * dy + dz * dz > rad * rad) continue;
                    int x = gx + dx, y = gy + dy, z = gz + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(N) || y >= static_cast<int>(N)
                        || z >= static_cast<int>(N))
                        continue;
                    uint8_t& v = voxelAt(x, y, z);
                    if (v == 0) solidCount++;
                    v = MatGlow;
                }
            }
        }
    }

    // Brick occupancy grid (any-solid per brick; empty bricks are skipped in
    // one coarse DDA step)
    for (uint32_t bz = 0; bz < BG; bz++) {
        for (uint32_t by = 0; by < BG; by++) {
            for (uint32_t bx = 0; bx < BG; bx++) {
                bool occupied = false;
                for (uint32_t z = bz * B; z < (bz + 1) * B && !occupied; z++) {
                    for (uint32_t y = by * B; y < (by + 1) * B && !occupied; y++) {
                        for (uint32_t x = bx * B; x < (bx + 1) * B; x++) {
                            if (voxelAt(x, y, z) != 0) {
                                occupied = true;
                                break;
                            }
                        }
                    }
                }
                occupancy[(static_cast<size_t>(bz) * BG + by) * BG + bx] = occupied ? 1 : 0;
            }
        }
    }

    dirty = true;

    if (auto* console = ConsoleSubsystem::Get()) {
        console->Info(
            fmt::format(
                "VoxelVolume generated: {}^3 grid, {} solid voxels ({:.1f}% fill)",
                N,
                solidCount,
                100.0f * static_cast<float>(solidCount) / static_cast<float>(static_cast<size_t>(N) * N * N)
            )
        );
    }
}

void VoxelVolumeComponent::CarveSphere(const glm::vec3& worldCenter, float radius) {
    if (volume.empty() || radius <= 0.0f) return;
    const int N = gridDim;
    const int B = brickDim;
    const int BG = N / B;

    // World -> voxel space (GetOrigin is the grid's world min corner).
    const glm::vec3 c = (worldCenter - GetOrigin()) / voxelSize;// sphere centre in voxels
    const float rv = radius / voxelSize;// radius in voxels
    const glm::ivec3 lo = glm::clamp(glm::ivec3(glm::floor(c - rv)), glm::ivec3(0), glm::ivec3(N - 1));
    const glm::ivec3 hi = glm::clamp(glm::ivec3(glm::ceil(c + rv)), glm::ivec3(0), glm::ivec3(N - 1));

    bool changed = false;
    const float rv2 = rv * rv;
    for (int z = lo.z; z <= hi.z; z++) {
        for (int y = lo.y; y <= hi.y; y++) {
            for (int x = lo.x; x <= hi.x; x++) {
                const glm::vec3 d = glm::vec3(x, y, z) + 0.5f - c;
                if (glm::dot(d, d) > rv2) continue;
                uint8_t& vx = volume[(static_cast<size_t>(z) * N + y) * N + x];
                if (vx != 0) {
                    vx = 0;
                    if (solidCount > 0) solidCount--;
                    changed = true;
                }
            }
        }
    }
    if (!changed) return;

    // Recompute occupancy for the affected brick range (a brick is occupied if
    // any voxel in it is still solid). Only the carved bricks are touched.
    const glm::ivec3 blo = lo / B;
    const glm::ivec3 bhi = hi / B;
    for (int bz = blo.z; bz <= bhi.z; bz++) {
        for (int by = blo.y; by <= bhi.y; by++) {
            for (int bx = blo.x; bx <= bhi.x; bx++) {
                bool occ = false;
                for (int z = bz * B; z < (bz + 1) * B && !occ; z++) {
                    for (int y = by * B; y < (by + 1) * B && !occ; y++) {
                        for (int x = bx * B; x < (bx + 1) * B; x++) {
                            if (volume[(static_cast<size_t>(z) * N + y) * N + x] != 0) {
                                occ = true;
                                break;
                            }
                        }
                    }
                }
                occupancy[(static_cast<size_t>(bz) * BG + by) * BG + bx] = occ ? 1 : 0;
            }
        }
    }
    dirty = true;// pass re-uploads the volume and resets GI history
}

bool VoxelVolumeComponent::RaycastVoxel(
    const glm::vec3& ro, const glm::vec3& rd, float maxDist, glm::vec3& outHitWorld
) const {
    if (volume.empty()) return false;
    const int N = gridDim;
    const glm::vec3 origin = GetOrigin();
    const glm::vec3 bmax = origin + glm::vec3(static_cast<float>(N) * voxelSize);

    // Ray-AABB (slab). rd is a normalized view ray; axis-aligned components are
    // vanishingly rare from a free camera, so 1/rd is fine here.
    const glm::vec3 invD = 1.0f / rd;
    const glm::vec3 t0 = (origin - ro) * invD;
    const glm::vec3 t1 = (bmax - ro) * invD;
    const glm::vec3 tsmall = glm::min(t0, t1);
    const glm::vec3 tbig = glm::max(t0, t1);
    const float tEnter = glm::max(glm::max(tsmall.x, tsmall.y), tsmall.z);
    const float tExit = glm::min(glm::min(tbig.x, tbig.y), tbig.z);
    if (tExit < glm::max(tEnter, 0.0f)) return false;

    float t = glm::max(tEnter, 0.0f) + voxelSize * 1e-3f;
    if (t > maxDist) return false;
    const glm::vec3 p = ro + rd * t;
    glm::ivec3 cell = glm::clamp(glm::ivec3(glm::floor((p - origin) / voxelSize)), glm::ivec3(0), glm::ivec3(N - 1));

    const glm::ivec3 step = glm::ivec3(glm::sign(rd));
    const glm::vec3 tDelta = glm::abs(glm::vec3(voxelSize) * invD);
    const glm::vec3 stepPos = glm::vec3(glm::greaterThan(rd, glm::vec3(0.0f)));
    const glm::vec3 boundary = origin + (glm::vec3(cell) + stepPos) * voxelSize;
    glm::vec3 tMax = (boundary - ro) * invD;

    for (int i = 0; i < 3 * N; i++) {
        if (volume[(static_cast<size_t>(cell.z) * N + cell.y) * N + cell.x] != 0) {
            outHitWorld = ro + rd * t;
            return true;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x;
            tMax.x += tDelta.x;
            cell.x += step.x;
            if (cell.x < 0 || cell.x >= N) break;
        } else if (tMax.y < tMax.z) {
            t = tMax.y;
            tMax.y += tDelta.y;
            cell.y += step.y;
            if (cell.y < 0 || cell.y >= N) break;
        } else {
            t = tMax.z;
            tMax.z += tDelta.z;
            cell.z += step.z;
            if (cell.z < 0 || cell.z >= N) break;
        }
        if (t > maxDist || t > tExit) break;
    }
    return false;
}

void VoxelVolumeComponent::OnAttach() {
    Generate(seed);
    if (auto* gs = GraphicsSubsystem::Get()) {
        if (auto* r = gs->renderer.get()) {
            if (auto* pass = r->GetPass<MicroVoxelPass>()) pass->RegisterVolume(this);
        }
    }
}

void VoxelVolumeComponent::OnDetach() {
    if (auto* gs = GraphicsSubsystem::Get()) {
        if (auto* r = gs->renderer.get()) {
            if (auto* pass = r->GetPass<MicroVoxelPass>()) pass->UnregisterVolume(this);
        }
    }
}

void VoxelVolumeComponent::DrawImGui() {
    ImGui::Text("%d^3 grid, %.0f cm voxels", gridDim, voxelSize * 100.0f);
    ImGui::Text(
        "%u solid (%.1f%% fill)",
        solidCount,
        100.0f * static_cast<float>(solidCount) / static_cast<float>(static_cast<size_t>(gridDim) * gridDim * gridDim)
    );
    if (ImGui::Button("Regenerate")) {
        Generate(seed + 1u);
    }
}

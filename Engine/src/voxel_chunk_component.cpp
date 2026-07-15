#include "voxel_chunk_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "material.hpp"
#include <algorithm>
#include <cstring>

VoxelChunkComponent::VoxelChunkComponent(
    GameObject* owner, GraphicsSubsystem* gfx, glm::ivec3 chunkPos, MaterialHandle material
)
  : _gfx(gfx), _chunkPos(chunkPos), _material(material) {
    gameObject = owner;
    std::memset(_voxels, 0, sizeof(_voxels));
    std::memset(_neighbors, 0, sizeof(_neighbors));
}

VoxelChunkComponent::~VoxelChunkComponent() {
    AssetManager::Get().UnregisterMesh(_meshHandle);
}

void VoxelChunkComponent::OnAttach() {
}
void VoxelChunkComponent::OnDetach() {
}

void VoxelChunkComponent::Relocate(glm::ivec3 newChunkPos) {
    std::memset(_neighbors, 0, sizeof(_neighbors));
    _chunkPos = newChunkPos;
    std::memset(_voxels, 0, sizeof(_voxels));
    _dirty = true;
    UploadMesh({});
    if (gameObject) gameObject->SetPosition(GetWorldPos());
}

uint8_t VoxelChunkComponent::GetVoxel(int x, int y, int z) const {
    if (!IsInBounds(x, y, z)) return 0;
    return _voxels[x][y][z];
}

void VoxelChunkComponent::SetVoxel(int x, int y, int z, uint8_t type) {
    if (!IsInBounds(x, y, z)) return;
    if (_voxels[x][y][z] != type) {
        _voxels[x][y][z] = type;
        _dirty = true;
    }
}

bool VoxelChunkComponent::IsAir(int x, int y, int z) const {
    return GetVoxel(x, y, z) == 0;
}

bool VoxelChunkComponent::IsInBounds(int x, int y, int z) const {
    return x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE;
}

void VoxelChunkComponent::SetNeighbor(int dx, int dy, int dz, VoxelChunkComponent* neighbor) {
    VoxelChunkComponent*& slot = _neighbors[dx + 1][dy + 1][dz + 1];
    if (slot == neighbor) return;
    slot = neighbor;
    // A newly-(un)linked neighbor changes what GetVoxelWithNeighbors() sees at this
    // chunk's border, so an already-built mesh is now stale and must be rebuilt --
    // otherwise a chunk meshed before its neighbor streamed in keeps a permanently
    // exposed boundary face (or, on unload, a permanently missing one).
    _dirty = true;
}

uint8_t VoxelChunkComponent::GetVoxelWithNeighbors(int x, int y, int z) const {
    int dx = 0, dy = 0, dz = 0;
    int nx = x, ny = y, nz = z;

    if (x < 0) {
        nx = x + SIZE;
        dx = -1;
    } else if (x >= SIZE) {
        nx = x - SIZE;
        dx = 1;
    }

    if (y < 0) {
        ny = y + SIZE;
        dy = -1;
    } else if (y >= SIZE) {
        ny = y - SIZE;
        dy = 1;
    }

    if (z < 0) {
        nz = z + SIZE;
        dz = -1;
    } else if (z >= SIZE) {
        nz = z - SIZE;
        dz = 1;
    }

    if (dx == 0 && dy == 0 && dz == 0) return _voxels[x][y][z];

    VoxelChunkComponent* nb = _neighbors[dx + 1][dy + 1][dz + 1];
    return nb ? nb->GetVoxel(nx, ny, nz) : 0;
}

glm::vec3 VoxelChunkComponent::GetBoundingSphereCenter() const {
    return GetWorldPos() + glm::vec3(SIZE * 0.5f);
}

float VoxelChunkComponent::GetBoundingSphereRadius() const {
    return BSPHERE_RADIUS;
}

void VoxelChunkComponent::RebuildMesh() {
    if (!_dirty) return;
    auto verts = GenerateMeshData();
    UploadMesh(verts);
}

std::vector<VoxelVertex> VoxelChunkComponent::GenerateMeshData() {
    VoxelMeshBuilder builder;
    for (int axis = 0; axis < 3; ++axis) {
        for (int layer = 0; layer < SIZE; ++layer) {
            BuildGreedyLayer(builder, axis, layer, +1);
            BuildGreedyLayer(builder, axis, layer, -1);
        }
    }
    return builder.Build();
}

void VoxelChunkComponent::UploadMesh(const std::vector<VoxelVertex>& verts) {
    if (!_mesh) {
        _mesh = std::make_unique<Mesh>(MeshType::VOXEL);
        _mesh->SetMaterial(_material);
        _meshHandle = AssetManager::Get().RegisterMesh(_mesh.get());
    }
    if (!verts.empty()) {
        _mesh->Update(verts);
    } else {
        // If the chunk becomes completely empty, clear GPU buffer
        _mesh->Update(std::vector<VoxelVertex>{});
    }

    glm::vec3 wp = GetWorldPos();
    auto s = static_cast<float>(SIZE);
    _mesh->SetBounds(wp, wp + glm::vec3(s));

    _dirty = false;
}

void VoxelChunkComponent::BuildGreedyLayer(VoxelMeshBuilder& builder, int axis, int layer, int dir) {
    int uAxis = (axis + 1) % 3;
    int vAxis = (axis + 2) % 3;
    uint8_t mask[SIZE][SIZE];
    uint8_t aoMask[SIZE][SIZE][4];// per-cell corner AO (C00, Cw0, Cwh, C0h), 0..3
    bool done[SIZE][SIZE];
    std::memset(mask, 0, sizeof(mask));

    // Is the voxel in the air-side layer (layer+dir) at u/v-plane coords solid?
    // AO occluders sit just outside the exposed face, in that adjacent layer.
    auto solidAir = [&](int uu, int vv) -> int {
        glm::ivec3 p(0);
        p[axis] = layer + dir;
        p[uAxis] = uu;
        p[vAxis] = vv;
        return GetVoxelWithNeighbors(p.x, p.y, p.z) != 0 ? 1 : 0;
    };
    // Minecraft-style vertex AO: two edge neighbors fully occlude the corner
    // together; otherwise each of the three neighbors darkens it one step.
    auto vertexAO = [](int side1, int side2, int corner) -> uint8_t {
        if (side1 != 0 && side2 != 0) return 0;
        return static_cast<uint8_t>(3 - side1 - side2 - corner);
    };

    for (int u = 0; u < SIZE; ++u) {
        for (int v = 0; v < SIZE; ++v) {
            glm::ivec3 posA(0), posB(0);
            posA[axis] = layer;
            posA[uAxis] = u;
            posA[vAxis] = v;
            posB = posA;
            posB[axis] += dir;

            uint8_t va = GetVoxelWithNeighbors(posA.x, posA.y, posA.z);
            uint8_t vb = GetVoxelWithNeighbors(posB.x, posB.y, posB.z);

            if (va != 0 && vb == 0) {
                mask[u][v] = va;
                // Sample the 8 neighbors around this cell in the air layer once,
                // then combine into the 4 corner AO values. um/up/vm/vp = the
                // four edge occluders; the diagonals are the corner occluders.
                int um = solidAir(u - 1, v), up = solidAir(u + 1, v);
                int vm = solidAir(u, v - 1), vp = solidAir(u, v + 1);
                int mm = solidAir(u - 1, v - 1), pm = solidAir(u + 1, v - 1);
                int pp = solidAir(u + 1, v + 1), mp = solidAir(u - 1, v + 1);
                aoMask[u][v][0] = vertexAO(um, vm, mm);// C00 (-u,-v)
                aoMask[u][v][1] = vertexAO(up, vm, pm);// Cw0 (+u,-v)
                aoMask[u][v][2] = vertexAO(up, vp, pp);// Cwh (+u,+v)
                aoMask[u][v][3] = vertexAO(um, vp, mp);// C0h (-u,+v)
            } else {
                mask[u][v] = 0;
            }
        }
    }

    FaceDir faceDir;
    if (axis == 1)
        faceDir = (dir > 0) ? FaceDir::TOP : FaceDir::BOTTOM;
    else if (axis == 0)
        faceDir = (dir > 0) ? FaceDir::RIGHT : FaceDir::LEFT;
    else
        faceDir = (dir > 0) ? FaceDir::FRONT : FaceDir::BACK;

    std::memset(done, 0, sizeof(done));

    // Two cells merge only if their voxel id AND all four corner AO values
    // match, so a merged quad is AO-uniform along the run — the darkening then
    // interpolates correctly across it (0fps "Meshing in a Minecraft world 2").
    auto aoEqual = [](const uint8_t a[4], const uint8_t b[4]) {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
    };

    for (int u = 0; u < SIZE; ++u) {
        for (int v = 0; v < SIZE; ++v) {
            if (done[u][v] || mask[u][v] == 0) continue;

            uint8_t voxelId = mask[u][v];
            const uint8_t* ao = aoMask[u][v];

            int w = 1;
            while (u + w < SIZE && mask[u + w][v] == voxelId && !done[u + w][v] && aoEqual(aoMask[u + w][v], ao))
                ++w;

            int h = 1;
            while (v + h < SIZE) {
                bool ok = true;
                for (int k = 0; k < w; ++k) {
                    if (mask[u + k][v + h] != voxelId || done[u + k][v + h] || !aoEqual(aoMask[u + k][v + h], ao)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
                ++h;
            }

            for (int du = 0; du < w; ++du)
                for (int dv = 0; dv < h; ++dv)
                    done[u + du][v + dv] = true;

            glm::ivec3 quadPos(0);
            quadPos[axis] = layer;
            quadPos[uAxis] = u;
            quadPos[vAxis] = v;

            builder.PushGreedyFace(quadPos, faceDir, voxelId, w, h, uAxis, vAxis, ao);
        }
    }
}

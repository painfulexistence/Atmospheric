#include "voxel_chunk_component.hpp"
#include "graphics_server.hpp"
#include "material.hpp"
#include <algorithm>
#include <cstring>

// Shared opaque material for all voxel chunks — no textures, just signals Opaque queue
static Material* s_voxelMaterial = nullptr;
static Material* GetVoxelMaterial() {
    if (!s_voxelMaterial) {
        s_voxelMaterial = new Material(MaterialProps{});
        s_voxelMaterial->renderQueue = RenderQueue::Opaque;
    }
    return s_voxelMaterial;
}

VoxelChunkComponent::VoxelChunkComponent(GameObject* owner, GraphicsServer* gfx, glm::ivec3 chunkPos)
    : _gfx(gfx), _chunkPos(chunkPos)
{
    gameObject = owner;
    std::memset(_voxels,    0, sizeof(_voxels));
    std::memset(_neighbors, 0, sizeof(_neighbors));
}

VoxelChunkComponent::~VoxelChunkComponent() {
    delete _mesh;
}

void VoxelChunkComponent::OnAttach() {}
void VoxelChunkComponent::OnDetach() {}

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

void VoxelChunkComponent::SetNeighbor(int dx, int dz, VoxelChunkComponent* neighbor) {
    _neighbors[dx + 1][dz + 1] = neighbor;
}

uint8_t VoxelChunkComponent::GetVoxelWithNeighbors(int x, int y, int z) const {
    if (y < 0 || y >= SIZE) return 0;

    int dx = 0, dz = 0;
    int nx = x, nz = z;

    if      (x <    0) { nx = x + SIZE; dx = -1; }
    else if (x >= SIZE) { nx = x - SIZE; dx =  1; }

    if      (z <    0) { nz = z + SIZE; dz = -1; }
    else if (z >= SIZE) { nz = z - SIZE; dz =  1; }

    if (dx == 0 && dz == 0) return _voxels[x][y][z];

    VoxelChunkComponent* nb = _neighbors[dx + 1][dz + 1];
    return nb ? nb->GetVoxel(nx, y, nz) : 0;
}

glm::vec3 VoxelChunkComponent::GetBoundingSphereCenter() const {
    return GetWorldPos() + glm::vec3(SIZE * 0.5f);
}

float VoxelChunkComponent::GetBoundingSphereRadius() const {
    return BSPHERE_RADIUS;
}

void VoxelChunkComponent::RebuildMesh() {
    if (!_dirty) return;

    VoxelMeshBuilder builder;

    for (int axis = 0; axis < 3; ++axis) {
        for (int layer = 0; layer < SIZE; ++layer) {
            BuildGreedyLayer(builder, axis, layer, +1);
            BuildGreedyLayer(builder, axis, layer, -1);
        }
    }

    const auto& verts = builder.Build();

    if (!_mesh) {
        _mesh = new Mesh(MeshType::VOXEL);
        _mesh->SetMaterial(GetVoxelMaterial());
    }
    if (!verts.empty()) {
        _mesh->Update(verts);
    }

    glm::vec3 wp = GetWorldPos();
    float s = static_cast<float>(SIZE);
    std::array<glm::vec3, 8> bounds = {{
        wp + glm::vec3(0, 0, 0), wp + glm::vec3(s, 0, 0),
        wp + glm::vec3(0, s, 0), wp + glm::vec3(s, s, 0),
        wp + glm::vec3(0, 0, s), wp + glm::vec3(s, 0, s),
        wp + glm::vec3(0, s, s), wp + glm::vec3(s, s, s),
    }};
    _mesh->SetBoundingBox(bounds);

    _dirty = false;
}

void VoxelChunkComponent::BuildGreedyLayer(VoxelMeshBuilder& builder,
                                            int axis, int layer, int dir)
{
    int u_axis = (axis + 1) % 3;
    int v_axis = (axis + 2) % 3;

    static uint8_t mask[SIZE][SIZE];
    static bool    done[SIZE][SIZE];
    std::memset(mask, 0, sizeof(mask));

    for (int u = 0; u < SIZE; ++u) {
        for (int v = 0; v < SIZE; ++v) {
            glm::ivec3 posA(0), posB(0);
            posA[axis]   = layer;
            posA[u_axis] = u;
            posA[v_axis] = v;
            posB         = posA;
            posB[axis]  += dir;

            uint8_t va = GetVoxelWithNeighbors(posA.x, posA.y, posA.z);
            uint8_t vb = GetVoxelWithNeighbors(posB.x, posB.y, posB.z);

            mask[u][v] = (va != 0 && vb == 0) ? va : 0;
        }
    }

    FaceDir faceDir;
    if (axis == 1)      faceDir = (dir > 0) ? FaceDir::TOP    : FaceDir::BOTTOM;
    else if (axis == 0) faceDir = (dir > 0) ? FaceDir::RIGHT  : FaceDir::LEFT;
    else                faceDir = (dir > 0) ? FaceDir::FRONT  : FaceDir::BACK;

    std::memset(done, 0, sizeof(done));

    for (int u = 0; u < SIZE; ++u) {
        for (int v = 0; v < SIZE; ++v) {
            if (done[u][v] || mask[u][v] == 0) continue;

            uint8_t voxelId = mask[u][v];

            int w = 1;
            while (u + w < SIZE && mask[u + w][v] == voxelId && !done[u + w][v])
                ++w;

            int h = 1;
            while (v + h < SIZE) {
                bool ok = true;
                for (int k = 0; k < w; ++k) {
                    if (mask[u + k][v + h] != voxelId || done[u + k][v + h]) {
                        ok = false; break;
                    }
                }
                if (!ok) break;
                ++h;
            }

            for (int du = 0; du < w; ++du)
                for (int dv = 0; dv < h; ++dv)
                    done[u + du][v + dv] = true;

            glm::ivec3 quadPos(0);
            quadPos[axis]   = layer;
            quadPos[u_axis] = u;
            quadPos[v_axis] = v;

            builder.PushGreedyFace(quadPos, faceDir, voxelId, w, h, u_axis, v_axis);
        }
    }
}

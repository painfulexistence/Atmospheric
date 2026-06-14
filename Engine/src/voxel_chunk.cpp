#include "voxel_chunk.hpp"
#include "graphics_server.hpp"
#include <algorithm>
#include <cstring>

VoxelChunk::VoxelChunk(glm::ivec3 chunkPos)
    : _chunkPos(chunkPos)
{
    std::memset(_voxels,    0, sizeof(_voxels));
    std::memset(_neighbors, 0, sizeof(_neighbors));
}

VoxelChunk::~VoxelChunk() {
    // _mesh lifetime is managed externally (passed to AssetManager / deleted by VoxelWorld)
    delete _mesh;
}

uint8_t VoxelChunk::GetVoxel(int x, int y, int z) const {
    if (!IsInBounds(x, y, z)) return 0;
    return _voxels[x][y][z];
}

void VoxelChunk::SetVoxel(int x, int y, int z, uint8_t type) {
    if (!IsInBounds(x, y, z)) return;
    if (_voxels[x][y][z] != type) {
        _voxels[x][y][z] = type;
        _dirty = true;
    }
}

bool VoxelChunk::IsAir(int x, int y, int z) const {
    return GetVoxel(x, y, z) == 0;
}

bool VoxelChunk::IsInBounds(int x, int y, int z) const {
    return x >= 0 && x < SIZE &&
           y >= 0 && y < SIZE &&
           z >= 0 && z < SIZE;
}

void VoxelChunk::SetNeighbor(int dx, int dz, VoxelChunk* chunk) {
    _neighbors[dx + 1][dz + 1] = chunk;
}

uint8_t VoxelChunk::GetVoxelWithNeighbors(int x, int y, int z) const {
    if (y < 0 || y >= SIZE) return 0;

    int dx = 0, dz = 0;
    int nx = x, nz = z;

    if      (x <    0) { nx = x + SIZE; dx = -1; }
    else if (x >= SIZE) { nx = x - SIZE; dx =  1; }

    if      (z <    0) { nz = z + SIZE; dz = -1; }
    else if (z >= SIZE) { nz = z - SIZE; dz =  1; }

    if (dx == 0 && dz == 0) return _voxels[x][y][z];

    VoxelChunk* nb = _neighbors[dx + 1][dz + 1];
    return nb ? nb->GetVoxel(nx, y, nz) : 0;
}

glm::vec3 VoxelChunk::GetBoundingSphereCenter() const {
    return GetWorldPos() + glm::vec3(SIZE * 0.5f);
}

float VoxelChunk::GetBoundingSphereRadius() const {
    return BSPHERE_RADIUS;
}

void VoxelChunk::RebuildMesh(GraphicsServer* gfx) {
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
    }
    if (!verts.empty()) {
        _mesh->Update(verts);
    }

    // Update AABB bounding box for frustum culling
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

void VoxelChunk::BuildGreedyLayer(VoxelMeshBuilder& builder,
                                   int axis, int layer, int dir)
{
    // The two axes perpendicular to `axis`
    int u_axis = (axis + 1) % 3;
    int v_axis = (axis + 2) % 3;

    // mask[u][v] = voxel_id of the visible face, 0 if none
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

            // Expand in the u direction
            int w = 1;
            while (u + w < SIZE && mask[u + w][v] == voxelId && !done[u + w][v])
                ++w;

            // Expand in the v direction (all u..u+w columns must match)
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

            // Mark consumed cells
            for (int du = 0; du < w; ++du)
                for (int dv = 0; dv < h; ++dv)
                    done[u + du][v + dv] = true;

            // Base corner of the quad in local chunk space
            glm::ivec3 quadPos(0);
            quadPos[axis]   = layer;
            quadPos[u_axis] = u;
            quadPos[v_axis] = v;

            builder.PushGreedyFace(quadPos, faceDir, voxelId, w, h, u_axis, v_axis);
        }
    }
}

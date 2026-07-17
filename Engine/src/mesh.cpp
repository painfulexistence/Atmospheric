#include "mesh.hpp"
#include "asset_manager.hpp"
#include "config.hpp"
#include "gfx_factory.hpp"
#include "graphics_subsystem.hpp"
#include "logging.hpp"

// Frustum culling (GraphicsSubsystem::Render) transforms this AABB to world
// space and p-vertex-tests it against the view frustum; without it a mesh
// reports empty bounds (min == max) and is never culled. MeshBuilder primitives
// set their bounds analytically after Initialize, so this only fills them in
// for imported / hand-built meshes (glTF, USD, .map) that would otherwise have
// none.
static AABB ComputeMeshAABB(const std::vector<Vertex>& verts) {
    if (verts.empty()) return {};
    glm::vec3 lo = verts[0].position, hi = verts[0].position;
    for (const Vertex& v : verts) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    return { lo, hi };
}

void PrintVertex(const Vertex& v) {
    ENGINE_INFO(
        "P: ({},{},{}), UV: ({},{})\n, N: ({},{},{}), T: ({},{},{}), B: ({},{},{})",
        v.position.x,
        v.position.y,
        v.position.z,
        v.uv.x,
        v.uv.y,
        v.normal.x,
        v.normal.y,
        v.normal.z,
        v.tangent.x,
        v.tangent.y,
        v.tangent.z,
        v.bitangent.x,
        v.bitangent.y,
        v.bitangent.z
    );
}

Mesh::Mesh(MeshType type) : type(type) {
    // No GL context exists under the WebGPU backend — geometry lives in the
    // RenderMesh Buffer (GPUBuffer) instead; vao/vbo/ebo/ibo stay 0.
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &ibo);
}

Mesh::~Mesh() {
    // Free GLBuffer if using new system
    if (_renderMeshHandle.IsValid()) {
        GraphicsSubsystem::Get()->FreeRenderMesh(_renderMeshHandle);
    }

    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &ibo);
    glDeleteVertexArrays(1, &vao);
    // _shape (unique_ptr<btCollisionShape>) frees itself here.
}

// Terrain mesh initialization
void Mesh::Initialize(const std::vector<Vertex>& verts) {
    vertCount = verts.size();
    triCount = 0;
    if (_bounds.IsEmpty()) _bounds = ComputeMeshAABB(verts);

    // WebGPU: no GL context — vertex-only geometry goes through the abstract
    // Buffer system, same as the indexed variant below. Terrain meshes take
    // this path (ForwardOpaquePass draws them with the non-tessellated
    // heightmap-displacement pipeline, mirroring terrain_simple.vert).
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_renderMeshHandle.IsValid()) {
            _renderMeshHandle =
                GraphicsSubsystem::Get()->AllocateRenderMesh(VertexFormat::Standard, BufferUsage::Static);
        }
        Buffer* renderMesh = GraphicsSubsystem::Get()->GetRenderMesh(_renderMeshHandle);
        if (renderMesh) {
            renderMesh->Upload(verts.data(), verts.size(), sizeof(Vertex));
        }
        this->initialized = true;
        return;
    }

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(5 * sizeof(float)));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(8 * sizeof(float)));
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(11 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    this->initialized = true;
}

void Mesh::Initialize(const std::vector<Vertex>& verts, const std::vector<uint16_t>& tris) {
    vertCount = verts.size();
    triCount = tris.size() / 3;
    if (_bounds.IsEmpty()) _bounds = ComputeMeshAABB(verts);

    // WebGPU: no GL context — geometry goes through the abstract Buffer
    // system only (ForwardOpaquePass/WaterPass draw from the render mesh).
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_renderMeshHandle.IsValid()) {
            _renderMeshHandle =
                GraphicsSubsystem::Get()->AllocateRenderMesh(VertexFormat::Standard, BufferUsage::Static);
        }
        Buffer* renderMesh = GraphicsSubsystem::Get()->GetRenderMesh(_renderMeshHandle);
        if (renderMesh) {
            renderMesh->Upload(verts.data(), verts.size(), sizeof(Vertex), tris.data(), tris.size());
        }
        this->initialized = true;
        return;
    }

    // Buffer binding reference:
    // https://stackoverflow.com/questions/17332657/does-a-vao-remember-both-a-ebo-ibo-elements-or-indices-and-a-vbo
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(5 * sizeof(float)));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(8 * sizeof(float)));
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(11 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glBindBuffer(GL_ARRAY_BUFFER, ibo);
    InstanceData dummyData{ glm::mat4(1.0f) };
    glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData), &dummyData, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), nullptr);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), reinterpret_cast<void*>(4 * sizeof(float)));
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), reinterpret_cast<void*>(8 * sizeof(float)));
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), reinterpret_cast<void*>(12 * sizeof(float)));
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    glEnableVertexAttribArray(7);
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(5, 1);
    glVertexAttribDivisor(6, 1);
    glVertexAttribDivisor(7, 1);
    glVertexAttribDivisor(8, 1);
#endif

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, tris.size() * sizeof(uint16_t), tris.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    this->initialized = true;
}

void Mesh::SetShapeLocalScaling(glm::vec3 localScaling) {
    _shape->setLocalScaling(btVector3(localScaling.x, localScaling.y, localScaling.z));
}

void Mesh::AddCapsuleShape(float radius, float height) {
    _shape = std::make_unique<btCapsuleShape>(radius, height);
}

void Mesh::Update(const std::vector<VoxelVertex>& vertices) {
    // Allocate GLBuffer on first use
    if (!_renderMeshHandle.IsValid()) {
        _renderMeshHandle = GraphicsSubsystem::Get()->AllocateRenderMesh(
            VertexFormat::Voxel, updateFreq == UpdateFrequency::Static ? BufferUsage::Static : BufferUsage::Dynamic
        );
    }

    // Get Buffer and upload data
    Buffer* renderMesh = GraphicsSubsystem::Get()->GetRenderMesh(_renderMeshHandle);
    if (renderMesh) {
        renderMesh->Upload(vertices.data(), vertices.size(), sizeof(VoxelVertex));
        vertCount = vertices.size();
        triCount = vertices.size() / 3;
        initialized = true;
    }
}


// Template method implementations for dynamic updates
template<typename VertexType> void Mesh::InitializeDynamic(GLenum primType) {
    _primitiveType = primType;

    // GL-only dynamic path (debug lines, canvas geometry); the WebGPU
    // consumers of these mesh types are guarded at the pass level.
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        initialized = true;
        return;
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Setup vertex attributes based on vertex type
    if constexpr (std::is_same_v<VertexType, DebugVertex>) {
        // DebugVertex: position (vec3) + color (vec3)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), nullptr);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(3 * sizeof(float))
        );
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    } else if constexpr (std::is_same_v<VertexType, CanvasVertex>) {
        // CanvasVertex: position (vec2) + texCoord (vec2) + color (vec4) + texIndex (int)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), nullptr);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), reinterpret_cast<void*>(2 * sizeof(float))
        );
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), reinterpret_cast<void*>(4 * sizeof(float))
        );
        glVertexAttribIPointer(3, 1, GL_INT, sizeof(CanvasVertex), reinterpret_cast<void*>(8 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
    } else if constexpr (std::is_same_v<VertexType, Vertex>) {
        // Standard Vertex: full vertex attributes
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(5 * sizeof(float)));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(8 * sizeof(float)));
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(11 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);
    }

    glBindVertexArray(0);
    initialized = true;
}

template<typename VertexType> void Mesh::UpdateDynamic(const std::vector<VertexType>& verts, GLenum primType) {
    if (!initialized) {
        InitializeDynamic<VertexType>(primType);
    }

    vertCount = verts.size();
    triCount = (primType == GL_TRIANGLES) ? verts.size() / 3 : 0;

    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        verts.size() * sizeof(VertexType),
        verts.data(),
        updateFreq == UpdateFrequency::Stream ? GL_STREAM_DRAW : GL_DYNAMIC_DRAW
    );
}

// Explicit template instantiations
template void Mesh::UpdateDynamic<DebugVertex>(const std::vector<DebugVertex>&, GLenum);
template void Mesh::UpdateDynamic<CanvasVertex>(const std::vector<CanvasVertex>&, GLenum);
template void Mesh::UpdateDynamic<Vertex>(const std::vector<Vertex>&, GLenum);

template void Mesh::InitializeDynamic<DebugVertex>(GLenum);
template void Mesh::InitializeDynamic<CanvasVertex>(GLenum);
template void Mesh::InitializeDynamic<Vertex>(GLenum);
// ── Instanced grass ──────────────────────────────────────────────────────────

void Mesh::InitGrassInstanced() {
    initialized = true;
    vertCount = 9;// canonical blade: 2 quads + tip triangle

    // Canonical blade in blade-local space: x = side in [-1,1], y = t in [0,1].
    // The vertex shader turns (side, t) + the per-instance transform into the
    // curved, wind-swayed world-space blade.
    const glm::vec2 blade[9] = {
        { -1.0f, 0.0f },  { 1.0f, 0.0f },  { -1.0f, 0.55f },// quad 1
        { 1.0f, 0.0f },   { 1.0f, 0.55f }, { -1.0f, 0.55f },// quad 2
        { -1.0f, 0.55f }, { 1.0f, 0.55f }, { 0.0f, 1.0f }// tip
    };

    // Both backends store the blade + instances in an RHI Grass-format
    // RenderMesh (slot 0 = blade, per-instance data via UploadInstances), and
    // Buffer::Draw emits the instanced draw. On WebGPU the data path is
    // complete but drawing still needs a GRASS WGSL pipeline in the forward
    // pass, so the WebGPU renderer skips GRASS meshes for now.
    if (!_renderMeshHandle.IsValid()) {
        _renderMeshHandle = GraphicsSubsystem::Get()->AllocateRenderMesh(VertexFormat::Grass, BufferUsage::Dynamic);
    }
    if (Buffer* renderMesh = GraphicsSubsystem::Get()->GetRenderMesh(_renderMeshHandle)) {
        renderMesh->Upload(blade, 9, sizeof(glm::vec2));
    }
}

void Mesh::UploadGrassInstances(const std::vector<GrassInstance>& instances) {
    instanceCount = instances.size();
    if (!_renderMeshHandle.IsValid()) return;
    if (Buffer* renderMesh = GraphicsSubsystem::Get()->GetRenderMesh(_renderMeshHandle)) {
        renderMesh->UploadInstances(instances.data(), instances.size(), sizeof(GrassInstance));
    }
}

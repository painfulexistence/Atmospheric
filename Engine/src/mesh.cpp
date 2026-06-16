#include "mesh.hpp"
#include "asset_manager.hpp"
#include "config.hpp"
#include "graphics_server.hpp"

void PrintVertex(const Vertex& v) {
    fmt::print(
      "P: ({},{},{}), UV: ({},{})\n, N: ({},{},{}), T: ({},{},{}), B: ({},{},{})\n",
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

Mesh::Mesh(MeshType type) : type(type), _material(nullptr), _shape(nullptr) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &ibo);
}

Mesh::~Mesh() {
    // Free GLBuffer if using new system
    if (_renderMeshHandle.IsValid()) {
        GraphicsServer::Get()->FreeRenderMesh(_renderMeshHandle);
    }

    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &ibo);
    glDeleteVertexArrays(1, &vao);
    // delete collisionShape; // FIXME: Should delete collisionShape somewhere else before the pointer is out of scope
}

// Terrain mesh initialization
void Mesh::Initialize(const std::vector<Vertex>& verts) {
    vertCount = verts.size();
    triCount = 0;

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(11 * sizeof(float)));
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
    // Buffer binding reference:
    // https://stackoverflow.com/questions/17332657/does-a-vao-remember-both-a-ebo-ibo-elements-or-indices-and-a-vbo
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(11 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
#ifndef __EMSCRIPTEN__
    glBindBuffer(GL_ARRAY_BUFFER, ibo);
    InstanceData dummyData{ glm::mat4(1.0f) };
    glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData), &dummyData, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), (void*)0);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), (void*)(4 * sizeof(float)));
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), (void*)(8 * sizeof(float)));
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(glm::vec4), (void*)(12 * sizeof(float)));
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
    _shape = new btCapsuleShape(radius, height);
}

void Mesh::Update(const std::vector<VoxelVertex>& vertices) {
    // Allocate GLBuffer on first use
    if (!_renderMeshHandle.IsValid()) {
        _renderMeshHandle = GraphicsServer::Get()->AllocateRenderMesh(
          VertexFormat::Voxel, updateFreq == UpdateFrequency::Static ? BufferUsage::Static : BufferUsage::Dynamic
        );
    }

    // Get Buffer and upload data
    Buffer* renderMesh = GraphicsServer::Get()->GetRenderMesh(_renderMeshHandle);
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

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Setup vertex attributes based on vertex type
    if constexpr (std::is_same_v<VertexType, DebugVertex>) {
        // DebugVertex: position (vec3) + color (vec3)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    } else if constexpr (std::is_same_v<VertexType, CanvasVertex>) {
        // CanvasVertex: position (vec2) + texCoord (vec2) + color (vec4) + texIndex (int)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)(2 * sizeof(float)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)(4 * sizeof(float)));
        glVertexAttribIPointer(3, 1, GL_INT, sizeof(CanvasVertex), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
    } else if constexpr (std::is_same_v<VertexType, Vertex>) {
        // Standard Vertex: full vertex attributes
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(11 * sizeof(float)));
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
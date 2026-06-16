#pragma once
#include "command_encoder.hpp"
#include <cstddef>
#include <cstdint>

// Vertex layout hint — tells the backend how to interpret interleaved vertex data.
enum class VertexFormat {
    Standard,  // Vertex: pos(vec3), uv(vec2), normal(vec3), tangent(vec3), bitangent(vec3)
    Debug,     // DebugVertex: pos(vec3), color(vec3)
    Canvas,    // CanvasVertex: pos2D(vec2), texCoord(vec2), color(vec4), texIndex(int), layer(int)
    Screen,    // ScreenVertex: pos2D(vec2), texCoord(vec2)
    Voxel      // VoxelVertex: pos(3xu8), voxel_id(u8), face_id(u8)
};

// Upload frequency hint — maps to driver hints like GL_STATIC_DRAW.
enum class BufferUsage {
    Static,   // Rarely or never updated after upload
    Dynamic,  // Updated occasionally
    Stream    // Updated every frame
};

// Backend-agnostic primitive assembly mode.
enum class PrimitiveTopology {
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
    Points,
};

// Opaque handle used to reference a buffer stored in a registry.
struct RenderMeshHandle {
    static constexpr uint32_t INVALID = UINT32_MAX;
    uint32_t id = INVALID;

    bool IsValid() const { return id != INVALID; }
    bool operator==(const RenderMeshHandle& other) const { return id == other.id; }
    bool operator!=(const RenderMeshHandle& other) const { return id != other.id; }
};

// Abstract GPU vertex/index buffer.
class Buffer {
public:
    virtual ~Buffer() = default;

    virtual void Initialize(VertexFormat format, BufferUsage usage = BufferUsage::Static) = 0;

    // Upload vertex-only geometry.
    virtual void Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) = 0;

    // Upload indexed geometry.
    virtual void Upload(
        const void* vertexData, size_t vertexCount, size_t vertexSize,
        const uint16_t* indexData, size_t indexCount) = 0;

    virtual void Draw(
        CommandEncoder* enc = nullptr,
        PrimitiveTopology topology = PrimitiveTopology::Triangles) const = 0;

    virtual bool IsInitialized() const = 0;
    virtual size_t GetVertexCount() const = 0;
    virtual size_t GetIndexCount() const = 0;
    virtual VertexFormat GetFormat() const = 0;
};

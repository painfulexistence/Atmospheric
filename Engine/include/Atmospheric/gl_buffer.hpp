#pragma once
#include "buffer.hpp"
#include "globals.hpp"

struct Vertex;
struct DebugVertex;
struct CanvasVertex;

// OpenGL implementation of Buffer.
// Manages a VAO + VBO + optional EBO backed by the GL driver.
class GLBuffer : public Buffer {
public:
    GLBuffer();
    ~GLBuffer() override;

    GLBuffer(const GLBuffer&) = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;
    GLBuffer(GLBuffer&& other) noexcept;
    GLBuffer& operator=(GLBuffer&& other) noexcept;

    void Initialize(VertexFormat format, BufferUsage usage = BufferUsage::Static) override;
    void Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) override;
    void Upload(
        const void* vertexData, size_t vertexCount, size_t vertexSize,
        const uint16_t* indexData, size_t indexCount) override;
    // enc is unused for GL — pass nullptr.
    void Draw(
        CommandEncoder* enc = nullptr,
        PrimitiveTopology topology = PrimitiveTopology::Triangles) const override;

    bool IsInitialized() const override { return _initialized; }
    size_t GetVertexCount() const override { return _vertexCount; }
    size_t GetIndexCount() const override { return _indexCount; }
    VertexFormat GetFormat() const override { return _format; }

    // OpenGL-specific: draw with a raw GL primitive type.
    void Draw(GLenum primitiveType) const;

    GLuint GetVAO() const { return _vao; }

private:
    void SetupVertexAttributes();
    GLenum GetGLUsage() const;
    void Cleanup();

    GLuint _vao = 0;
    GLuint _vbo = 0;
    GLuint _ebo = 0;

    VertexFormat _format = VertexFormat::Standard;
    BufferUsage  _usage  = BufferUsage::Static;

    size_t _vertexCount = 0;
    size_t _indexCount  = 0;
    bool _initialized   = false;
    bool _hasIndices    = false;
};

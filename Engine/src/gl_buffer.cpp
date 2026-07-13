#include "gl_buffer.hpp"
#include "graphics_subsystem.hpp"
#include "vertex.hpp"

static GLenum ToGLTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::Lines:
        return GL_LINES;
    case PrimitiveTopology::LineStrip:
        return GL_LINE_STRIP;
    case PrimitiveTopology::Points:
        return GL_POINTS;
    case PrimitiveTopology::Triangles:
    default:
        return GL_TRIANGLES;
    }
}

GLBuffer::GLBuffer() {
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ebo);
}

GLBuffer::~GLBuffer() {
    Cleanup();
}

GLBuffer::GLBuffer(GLBuffer&& other) noexcept
  : _vao(other._vao), _vbo(other._vbo), _ebo(other._ebo), _instanceVBO(other._instanceVBO), _format(other._format),
    _usage(other._usage), _vertexCount(other._vertexCount), _indexCount(other._indexCount),
    _instanceCount(other._instanceCount), _initialized(other._initialized), _hasIndices(other._hasIndices) {
    other._vao = 0;
    other._vbo = 0;
    other._ebo = 0;
    other._instanceVBO = 0;
    other._initialized = false;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
    if (this != &other) {
        Cleanup();
        _vao = other._vao;
        _vbo = other._vbo;
        _ebo = other._ebo;
        _instanceVBO = other._instanceVBO;
        _format = other._format;
        _usage = other._usage;
        _vertexCount = other._vertexCount;
        _indexCount = other._indexCount;
        _instanceCount = other._instanceCount;
        _initialized = other._initialized;
        _hasIndices = other._hasIndices;
        other._vao = 0;
        other._vbo = 0;
        other._ebo = 0;
        other._instanceVBO = 0;
        other._initialized = false;
    }
    return *this;
}

void GLBuffer::Cleanup() {
    if (_vbo) {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_ebo) {
        glDeleteBuffers(1, &_ebo);
        _ebo = 0;
    }
    if (_instanceVBO) {
        glDeleteBuffers(1, &_instanceVBO);
        _instanceVBO = 0;
    }
    if (_vao) {
        glDeleteVertexArrays(1, &_vao);
        _vao = 0;
    }
}

void GLBuffer::Initialize(VertexFormat format, BufferUsage usage) {
    _format = format;
    _usage = usage;
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    SetupVertexAttributes();
    glBindVertexArray(0);
    _initialized = true;
}

void GLBuffer::SetupVertexAttributes() {
    switch (_format) {
    case VertexFormat::Standard:
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
        break;

    case VertexFormat::Debug:
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), nullptr);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), reinterpret_cast<void*>(3 * sizeof(float))
        );
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        break;

    case VertexFormat::Canvas:
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
        break;

    case VertexFormat::Screen:
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), nullptr);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), reinterpret_cast<void*>(offsetof(ScreenVertex, texCoord))
        );
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        break;

    case VertexFormat::Voxel:
        glVertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), nullptr);
        glVertexAttribIPointer(
            1, 1, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), reinterpret_cast<void*>(3 * sizeof(uint8_t))
        );
        glVertexAttribIPointer(
            2, 1, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), reinterpret_cast<void*>(4 * sizeof(uint8_t))
        );
        glVertexAttribIPointer(
            3, 1, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), reinterpret_cast<void*>(5 * sizeof(uint8_t))
        );
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
        break;

    case VertexFormat::Grass:
        // Canonical blade corner: (side, t) — see grass.vert.
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        break;
    }
}

bool GLBuffer::SetupInstanceAttributes() {
    switch (_format) {
    case VertexFormat::Grass:
        // GrassInstance: (root.xyz, facing) @5, (length, lean, phase, hue) @6.
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(GrassInstance), nullptr);
        glVertexAttribPointer(
            6, 4, GL_FLOAT, GL_FALSE, sizeof(GrassInstance), reinterpret_cast<void*>(4 * sizeof(float))
        );
        glEnableVertexAttribArray(5);
        glEnableVertexAttribArray(6);
        glVertexAttribDivisor(5, 1);
        glVertexAttribDivisor(6, 1);
        return true;
    default:
        return false;// format has no per-instance layout
    }
}

GLenum GLBuffer::GetGLUsage() const {
    switch (_usage) {
    case BufferUsage::Static:
        return GL_STATIC_DRAW;
    case BufferUsage::Dynamic:
        return GL_DYNAMIC_DRAW;
    case BufferUsage::Stream:
        return GL_STREAM_DRAW;
    default:
        return GL_STATIC_DRAW;
    }
}

void GLBuffer::Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) {
    if (!_initialized) Initialize(_format, _usage);
    _vertexCount = vertexCount;
    _hasIndices = false;
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexSize, vertexData, GetGLUsage());
}

void GLBuffer::Upload(
    const void* vertexData, size_t vertexCount, size_t vertexSize, const uint16_t* indexData, size_t indexCount
) {
    if (!_initialized) Initialize(_format, _usage);
    _vertexCount = vertexCount;
    _indexCount = indexCount;
    _hasIndices = true;
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexSize, vertexData, GetGLUsage());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(uint16_t), indexData, GetGLUsage());
    glBindVertexArray(0);
}

void GLBuffer::UploadInstances(const void* instanceData, size_t instanceCount, size_t instanceSize) {
    if (!_initialized) Initialize(_format, _usage);

    // Empty upload: record the count and keep any previous buffer/attribute
    // state. Never glBufferData a 0-byte instance buffer — the VAO's enabled
    // divisor attributes would point at empty storage, and a later draw that
    // touches them reads out of bounds (Apple's GL falls back to a
    // copy-through submit and crashes dereferencing base+offset).
    if (instanceCount == 0) {
        _instanceCount = 0;
        return;
    }

    const bool firstUpload = (_instanceVBO == 0);
    if (firstUpload) glGenBuffers(1, &_instanceVBO);

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _instanceVBO);
    if (firstUpload && !SetupInstanceAttributes()) {
        // Format has no instance layout — drop the buffer, stay non-instanced.
        glBindVertexArray(0);
        glDeleteBuffers(1, &_instanceVBO);
        _instanceVBO = 0;
        _instanceCount = 0;
        return;
    }
    glBufferData(GL_ARRAY_BUFFER, instanceCount * instanceSize, instanceData, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    _instanceCount = instanceCount;
}

void GLBuffer::Draw(CommandEncoder* /*enc*/, PrimitiveTopology topology) const {
    Draw(ToGLTopology(topology));
}

void GLBuffer::Draw(GLenum primitiveType) const {
    // Once an instance buffer exists this VAO has divisor attributes enabled,
    // so it must ALWAYS draw instanced — a plain draw would still fetch
    // instance slot 0 from that buffer (out of bounds when the live count is
    // 0). Zero instances therefore means draw nothing, not draw once.
    if (_instanceVBO) {
        if (_instanceCount == 0) return;
        glBindVertexArray(_vao);
        if (_hasIndices) {
            glDrawElementsInstanced(
                primitiveType,
                static_cast<GLsizei>(_indexCount),
                GL_UNSIGNED_SHORT,
                nullptr,
                static_cast<GLsizei>(_instanceCount)
            );
        } else {
            glDrawArraysInstanced(
                primitiveType, 0, static_cast<GLsizei>(_vertexCount), static_cast<GLsizei>(_instanceCount)
            );
        }
        glBindVertexArray(0);
        return;
    }
    glBindVertexArray(_vao);
    if (_hasIndices) {
        glDrawElements(primitiveType, static_cast<GLsizei>(_indexCount), GL_UNSIGNED_SHORT, nullptr);
    } else {
        glDrawArrays(primitiveType, 0, static_cast<GLsizei>(_vertexCount));
    }
    glBindVertexArray(0);
}

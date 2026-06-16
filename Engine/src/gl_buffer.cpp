#include "gl_buffer.hpp"
#include "vertex.hpp"
#include "graphics_server.hpp"

static GLenum ToGLTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::Lines:         return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::Points:        return GL_POINTS;
        case PrimitiveTopology::Triangles:
        default:                               return GL_TRIANGLES;
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
    : _vao(other._vao)
    , _vbo(other._vbo)
    , _ebo(other._ebo)
    , _format(other._format)
    , _usage(other._usage)
    , _vertexCount(other._vertexCount)
    , _indexCount(other._indexCount)
    , _initialized(other._initialized)
    , _hasIndices(other._hasIndices)
{
    other._vao = 0;
    other._vbo = 0;
    other._ebo = 0;
    other._initialized = false;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
    if (this != &other) {
        Cleanup();
        _vao         = other._vao;
        _vbo         = other._vbo;
        _ebo         = other._ebo;
        _format      = other._format;
        _usage       = other._usage;
        _vertexCount = other._vertexCount;
        _indexCount  = other._indexCount;
        _initialized = other._initialized;
        _hasIndices  = other._hasIndices;
        other._vao = 0;
        other._vbo = 0;
        other._ebo = 0;
        other._initialized = false;
    }
    return *this;
}

void GLBuffer::Cleanup() {
    if (_vbo) { glDeleteBuffers(1, &_vbo);       _vbo = 0; }
    if (_ebo) { glDeleteBuffers(1, &_ebo);       _ebo = 0; }
    if (_vao) { glDeleteVertexArrays(1, &_vao);  _vao = 0; }
}

void GLBuffer::Initialize(VertexFormat format, BufferUsage usage) {
    _format = format;
    _usage  = usage;
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    SetupVertexAttributes();
    glBindVertexArray(0);
    _initialized = true;
}

void GLBuffer::SetupVertexAttributes() {
    switch (_format) {
        case VertexFormat::Standard:
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
            break;

        case VertexFormat::Debug:
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            break;

        case VertexFormat::Canvas:
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)(2 * sizeof(float)));
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CanvasVertex), (void*)(4 * sizeof(float)));
            glVertexAttribIPointer(3, 1, GL_INT,  sizeof(CanvasVertex), (void*)(8 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glEnableVertexAttribArray(3);
            break;

        case VertexFormat::Screen:
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), (void*)0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenVertex), (void*)offsetof(ScreenVertex, texCoord));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            break;

        case VertexFormat::Voxel:
            glVertexAttribIPointer(0, 3, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), (void*)0);
            glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), (void*)(3 * sizeof(uint8_t)));
            glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), (void*)(4 * sizeof(uint8_t)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            break;
    }
}

GLenum GLBuffer::GetGLUsage() const {
    switch (_usage) {
        case BufferUsage::Static:  return GL_STATIC_DRAW;
        case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
        case BufferUsage::Stream:  return GL_STREAM_DRAW;
        default:                   return GL_STATIC_DRAW;
    }
}

void GLBuffer::Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) {
    if (!_initialized) Initialize(_format, _usage);
    _vertexCount = vertexCount;
    _hasIndices  = false;
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexSize, vertexData, GetGLUsage());
}

void GLBuffer::Upload(const void* vertexData, size_t vertexCount, size_t vertexSize,
                      const uint16_t* indexData, size_t indexCount) {
    if (!_initialized) Initialize(_format, _usage);
    _vertexCount = vertexCount;
    _indexCount  = indexCount;
    _hasIndices  = true;
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * vertexSize, vertexData, GetGLUsage());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(uint16_t), indexData, GetGLUsage());
    glBindVertexArray(0);
}

void GLBuffer::Draw(CommandEncoder* /*enc*/, PrimitiveTopology topology) const {
    Draw(ToGLTopology(topology));
}

void GLBuffer::Draw(GLenum primitiveType) const {
    glBindVertexArray(_vao);
    if (_hasIndices) {
        glDrawElements(primitiveType, static_cast<GLsizei>(_indexCount), GL_UNSIGNED_SHORT, nullptr);
    } else {
        glDrawArrays(primitiveType, 0, static_cast<GLsizei>(_vertexCount));
    }
    glBindVertexArray(0);
}

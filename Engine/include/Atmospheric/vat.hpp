#pragma once
#include "globals.hpp"
#include <glm/vec3.hpp>
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Animation Texture (VAT)
//
// A VAT stores a mesh's per-vertex animation in textures instead of a skeleton:
// one texel per (vertex, frame). Houdini's VAT ROP (SideFX Labs) is the usual
// authoring path for cloth/soft-body/fluid/rigid-fracture sims, but the format
// is trivial, so this engine can also bake one procedurally at runtime (see the
// 3DBasics demo). Playback is a pure vertex-stage texture fetch (vat.vert), so
// it needs no compute shaders and runs on every GL/GLES/WebGL2 target.
//
// This class owns the two float textures (position + normal) for one clip. The
// vertex ordering baked here MUST match the mesh's vertex buffer ordering,
// because vat.vert addresses columns by gl_VertexID.
// ─────────────────────────────────────────────────────────────────────────────

// Raw per-frame animation data fed to VATClip::Bake. positions[f][v] and
// normals[f][v] are object-space; every frame must contain the same vertCount
// entries, in the mesh's vertex-buffer order.
struct VATFrameData {
    std::vector<std::vector<glm::vec3>> positions;
    std::vector<std::vector<glm::vec3>> normals;
    float frameRate = 30.0f;// frames per second, defines playback duration
};

class VATClip {
public:
    // Non-copyable: owns GL texture objects.
    VATClip() = default;
    VATClip(const VATClip&) = delete;
    VATClip& operator=(const VATClip&) = delete;
    ~VATClip();

    // Uploads frame data into RGB32F position/normal textures. Returns nullptr
    // if the data is empty, ragged (frames of differing vertex counts), or the
    // vertex count exceeds GL_MAX_TEXTURE_SIZE. Positions/normals are stored raw
    // (object space); the shader's remap path is left disabled.
    static std::unique_ptr<VATClip> Bake(const VATFrameData& data);

    GLuint GetPositionTexture() const {
        return _positionTex;
    }
    GLuint GetNormalTexture() const {
        return _normalTex;
    }
    uint32_t GetVertCount() const {
        return _vertCount;
    }
    uint32_t GetFrameCount() const {
        return _frameCount;
    }
    float GetFrameRate() const {
        return _frameRate;
    }
    // Playback duration in seconds; 0 for a single-frame clip.
    float GetDuration() const {
        return _frameRate > 0.0f ? static_cast<float>(_frameCount) / _frameRate : 0.0f;
    }

private:
    GLuint _positionTex = 0;
    GLuint _normalTex = 0;
    uint32_t _vertCount = 0;
    uint32_t _frameCount = 0;
    float _frameRate = 30.0f;
};

#include "vat.hpp"
#include "console_subsystem.hpp"
#include "globals.hpp"

namespace {
// Packs positions[f][v] / normals[f][v] into a flat, row-major RGB float array
// laid out as (frame, vertex): row f holds every vertex for that frame, which
// matches the (u = vertex, v = frame) sampling in vat.vert.
std::vector<float> Flatten(const std::vector<std::vector<glm::vec3>>& frames, uint32_t vertCount) {
    std::vector<float> out;
    out.reserve(static_cast<size_t>(frames.size()) * vertCount * 3);
    for (const auto& frame : frames) {
        for (uint32_t v = 0; v < vertCount; ++v) {
            out.push_back(frame[v].x);
            out.push_back(frame[v].y);
            out.push_back(frame[v].z);
        }
    }
    return out;
}

// Creates a NEAREST-filtered, clamped RGB32F texture from a flat float array.
GLuint UploadFloatTexture(const std::vector<float>& data, uint32_t width, uint32_t height) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // NEAREST + CLAMP: float textures are not guaranteed linear-filterable on
    // GLES/WebGL2, and vat.vert does its own frame interpolation anyway.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGB32F, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGB, GL_FLOAT,
        data.data()
    );
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
}// namespace

VATClip::~VATClip() {
    if (_positionTex) glDeleteTextures(1, &_positionTex);
    if (_normalTex) glDeleteTextures(1, &_normalTex);
}

std::unique_ptr<VATClip> VATClip::Bake(const VATFrameData& data) {
    const size_t frameCount = data.positions.size();
    if (frameCount == 0 || data.positions[0].empty()) {
        ConsoleSubsystem::Get()->Error("[Engine] VATClip::Bake: empty frame data");
        return nullptr;
    }
    if (data.normals.size() != frameCount) {
        ConsoleSubsystem::Get()->Error(fmt::format("[Engine] VATClip::Bake: positions/normals frame count mismatch ({} vs {})", frameCount, data.normals.size()));
        return nullptr;
    }

    const size_t vertCount = data.positions[0].size();
    for (size_t f = 0; f < frameCount; ++f) {
        if (data.positions[f].size() != vertCount || data.normals[f].size() != vertCount) {
            ConsoleSubsystem::Get()->Error(fmt::format("[Engine] VATClip::Bake: ragged frame {} (expected {} verts)", f, vertCount));
            return nullptr;
        }
    }

    GLint maxTexSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    if (static_cast<GLint>(vertCount) > maxTexSize || static_cast<GLint>(frameCount) > maxTexSize) {
        ConsoleSubsystem::Get()->Error(fmt::format(
            "[Engine] VATClip::Bake: {}x{} exceeds GL_MAX_TEXTURE_SIZE ({}); split the clip",
            vertCount, frameCount, maxTexSize
        ));
        return nullptr;
    }

    auto clip = std::make_unique<VATClip>();
    clip->_vertCount = static_cast<uint32_t>(vertCount);
    clip->_frameCount = static_cast<uint32_t>(frameCount);
    clip->_frameRate = data.frameRate;

    const std::vector<float> posData = Flatten(data.positions, clip->_vertCount);
    const std::vector<float> normData = Flatten(data.normals, clip->_vertCount);
    clip->_positionTex = UploadFloatTexture(posData, clip->_vertCount, clip->_frameCount);
    clip->_normalTex = UploadFloatTexture(normData, clip->_vertCount, clip->_frameCount);

    ENGINE_LOG("VATClip::Bake: {} verts x {} frames @ {} fps", vertCount, frameCount, data.frameRate);
    return clip;
}

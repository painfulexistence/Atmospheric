#include "vat.hpp"
#include "console_subsystem.hpp"
#include "gfx_factory.hpp"
#include "globals.hpp"

namespace {
    // Packs positions[f][v] / normals[f][v] into a flat, row-major RGBA float array
    // laid out as (frame, vertex): row f holds every vertex for that frame, which
    // matches the (u = vertex, v = frame) addressing in vat.vert / VAT_WGSL. Alpha
    // is unused padding (RGBA is required because WebGPU has no rgb32float format).
    std::vector<float> Flatten(const std::vector<std::vector<glm::vec3>>& frames, uint32_t vertCount) {
        std::vector<float> out;
        out.reserve(static_cast<size_t>(frames.size()) * vertCount * 4);
        for (const auto& frame : frames) {
            for (uint32_t v = 0; v < vertCount; ++v) {
                out.push_back(frame[v].x);
                out.push_back(frame[v].y);
                out.push_back(frame[v].z);
                out.push_back(0.0f);
            }
        }
        return out;
    }
}// namespace

VATClip::~VATClip() {
    // GfxFactory owns the backend texture object (GL handle or WGPUTexture);
    // release through it so both backends are handled uniformly.
    if (_positionTex) GfxFactory::ReleaseTexture(_positionTex);
    if (_normalTex) GfxFactory::ReleaseTexture(_normalTex);
}

std::unique_ptr<VATClip> VATClip::Bake(const VATFrameData& data) {
    const size_t frameCount = data.positions.size();
    if (frameCount == 0 || data.positions[0].empty()) {
        ConsoleSubsystem::Get()->Error("[Engine] VATClip::Bake: empty frame data");
        return nullptr;
    }
    if (data.normals.size() != frameCount) {
        ConsoleSubsystem::Get()->Error(
            fmt::format(
                "[Engine] VATClip::Bake: positions/normals frame count mismatch ({} vs {})",
                frameCount,
                data.normals.size()
            )
        );
        return nullptr;
    }

    const size_t vertCount = data.positions[0].size();
    for (size_t f = 0; f < frameCount; ++f) {
        if (data.positions[f].size() != vertCount || data.normals[f].size() != vertCount) {
            ConsoleSubsystem::Get()->Error(
                fmt::format("[Engine] VATClip::Bake: ragged frame {} (expected {} verts)", f, vertCount)
            );
            return nullptr;
        }
    }

    auto clip = std::make_unique<VATClip>();
    clip->_vertCount = static_cast<uint32_t>(vertCount);
    clip->_frameCount = static_cast<uint32_t>(frameCount);
    clip->_frameRate = data.frameRate;

    const std::vector<float> posData = Flatten(data.positions, clip->_vertCount);
    const std::vector<float> normData = Flatten(data.normals, clip->_vertCount);
    clip->_positionTex = GfxFactory::UploadTextureRGBA32F(
        posData.data(), static_cast<int>(clip->_vertCount), static_cast<int>(clip->_frameCount)
    );
    clip->_normalTex = GfxFactory::UploadTextureRGBA32F(
        normData.data(), static_cast<int>(clip->_vertCount), static_cast<int>(clip->_frameCount)
    );

    ENGINE_LOG("VATClip::Bake: {} verts x {} frames @ {} fps", vertCount, frameCount, data.frameRate);
    return clip;
}

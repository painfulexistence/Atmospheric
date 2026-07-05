#include "rmlui_renderer.hpp"
#include "gfx_factory.hpp"
#include "log.hpp"
#include "renderer.hpp"
#include <RmlUi/Core.h>
#include <spdlog/spdlog.h>

RmlUiRenderer::RmlUiRenderer(Renderer* renderer) : _Renderer(renderer) {
}

RmlUiRenderer::~RmlUiRenderer() {
    Shutdown();
}

void RmlUiRenderer::Initialize() {
    Log::Info("RmlUi renderer initialized (Adapter mode)");
}

void RmlUiRenderer::Shutdown() {
    _textures.clear();
    _geometry.clear();
}

Rml::CompiledGeometryHandle
    RmlUiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    CompiledGeometry geom;
    geom.vertices.reserve(vertices.size());
    geom.indices.reserve(indices.size());

    for (const auto& v : vertices) {
        BatchVertex bv;
        bv.position = glm::vec3(v.position.x, v.position.y, 0.0f);
        // RmlUi color is 0-255, BatchVertex expects 0-1 float?
        // Wait, BatchVertex color is glm::vec4.
        // Rml::Colourb is 4 bytes.
        bv.color =
            glm::vec4(v.colour.red / 255.0f, v.colour.green / 255.0f, v.colour.blue / 255.0f, v.colour.alpha / 255.0f);
        bv.uv = glm::vec2(v.tex_coord.x, v.tex_coord.y);
        bv.texIndex = 0.0f;// Set later
        bv.entityID = -1.0f;
        geom.vertices.push_back(bv);
    }

    for (int i : indices) {
        geom.indices.push_back(static_cast<uint32_t>(i));
    }

    Rml::CompiledGeometryHandle handle = _next_geometry_handle++;
    _geometry[handle] = std::move(geom);

    return handle;
}

void RmlUiRenderer::RenderGeometry(
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture
) {
    auto it = _geometry.find(geometry);
    if (it == _geometry.end()) return;

    const CompiledGeometry& geom = it->second;

    // Apply translation
    glm::mat4 transform = glm::translate(_transform, glm::vec3(translation.x, translation.y, 0.0f));

    // Submit to Renderer
    if (_Renderer) {
        BatchDrawCommand cmd;
        cmd.vertices = geom.vertices;
        cmd.indices = geom.indices;
        cmd.textureID = static_cast<uint32_t>(texture);
        cmd.transform = transform;
        _Renderer->SubmitUICommand(cmd);
    }
}

void RmlUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    _geometry.erase(geometry);
}

void RmlUiRenderer::EnableScissorRegion(bool enable) {
    _scissor.enabled = enable;
    // TODO: Pass scissor command to BatchRenderer or Renderer
    // For now, BatchRenderer doesn't support scissor per draw call easily without flushing.
    // We might need to add SetScissor to BatchRenderer.
}

void RmlUiRenderer::SetScissorRegion(Rml::Rectanglei region) {
    _scissor.x = region.Left();
    _scissor.y = region.Top();
    _scissor.width = region.Width();
    _scissor.height = region.Height();
    // TODO: Pass scissor command
}

Rml::TextureHandle RmlUiRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    // In a real engine, we would load the texture via AssetManager
    // For now, we return 0 or implement basic loading if needed
    // But since we are refactoring, let's keep it minimal
    Log::Warn("RmlUi texture loading not fully implemented: {}", source);
    return 0;
}

Rml::TextureHandle RmlUiRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        uint32_t texture_id = GfxFactory::UploadTexture2D(
            reinterpret_cast<const uint8_t*>(source.data()), source_dimensions.x, source_dimensions.y
        );
        return (Rml::TextureHandle)texture_id;
    }
#endif

    // OpenGL path: GfxFactory::UploadTexture2D's GL fallback uses GL_NEAREST,
    // which would regress RmlUi's text/UI rendering quality — upload directly
    // with GL_LINEAR filtering instead, as before.
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        source_dimensions.x,
        source_dimensions.y,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        source.data()
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    return static_cast<Rml::TextureHandle>(textureId);
}

void RmlUiRenderer::ReleaseTexture(Rml::TextureHandle texture_handle) {
    GfxFactory::ReleaseTexture(static_cast<uint32_t>(texture_handle));
}

void RmlUiRenderer::SetTransform(const Rml::Matrix4f* transform) {
    if (transform) {
        _transform = glm::make_mat4(transform->data());
    } else {
        _transform = glm::mat4(1.0f);
    }
}

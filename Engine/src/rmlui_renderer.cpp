#include "rmlui_renderer.hpp"
#include "renderer.hpp"
#include <RmlUi/Core.h>
#include <spdlog/spdlog.h>

RmlUiRenderer::RmlUiRenderer(Renderer* renderer) : m_Renderer(renderer) {
}

RmlUiRenderer::~RmlUiRenderer() {
    Shutdown();
}

void RmlUiRenderer::Initialize() {
    spdlog::info("RmlUi renderer initialized (Adapter mode)");
}

void RmlUiRenderer::Shutdown() {
    m_textures.clear();
    m_geometry.clear();
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
        geom.indices.push_back((uint32_t)i);
    }

    Rml::CompiledGeometryHandle handle = m_next_geometry_handle++;
    m_geometry[handle] = std::move(geom);

    return handle;
}

void RmlUiRenderer::RenderGeometry(
  Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture
) {
    auto it = m_geometry.find(geometry);
    if (it == m_geometry.end()) return;

    const CompiledGeometry& geom = it->second;

    // Apply translation
    glm::mat4 transform = glm::translate(m_transform, glm::vec3(translation.x, translation.y, 0.0f));

    // Submit to Renderer
    if (m_Renderer) {
        BatchDrawCommand cmd;
        cmd.vertices = geom.vertices;
        cmd.indices = geom.indices;
        cmd.textureID = (uint32_t)texture;
        cmd.transform = transform;
        m_Renderer->SubmitUICommand(cmd);
    }
}

void RmlUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    m_geometry.erase(geometry);
}

void RmlUiRenderer::EnableScissorRegion(bool enable) {
    m_scissor.enabled = enable;
    // TODO: Pass scissor command to BatchRenderer or Renderer
    // For now, BatchRenderer doesn't support scissor per draw call easily without flushing.
    // We might need to add SetScissor to BatchRenderer.
}

void RmlUiRenderer::SetScissorRegion(Rml::Rectanglei region) {
    m_scissor.x = region.Left();
    m_scissor.y = region.Top();
    m_scissor.width = region.Width();
    m_scissor.height = region.Height();
    // TODO: Pass scissor command
}

Rml::TextureHandle RmlUiRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    // In a real engine, we would load the texture via AssetManager
    // For now, we return 0 or implement basic loading if needed
    // But since we are refactoring, let's keep it minimal
    spdlog::warn("RmlUi texture loading not fully implemented: {}", source);
    return 0;
}

Rml::TextureHandle RmlUiRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
    // We need to create an OpenGL texture and return its ID
    // But we shouldn't call OpenGL directly here ideally?
    // Well, creating resources is fine, drawing is the issue.
    // Or we can ask Renderer to create it.

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA8, source_dimensions.x, source_dimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.data()
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    Rml::TextureHandle texture_handle = (Rml::TextureHandle)texture_id;
    // We don't need to store it in m_textures if we just cast ID to handle
    // But RmlUi expects us to manage handles.
    // Let's just return the GL ID as handle for simplicity with BatchRenderer
    return texture_handle;
}

void RmlUiRenderer::ReleaseTexture(Rml::TextureHandle texture_handle) {
    GLuint id = (GLuint)texture_handle;
    glDeleteTextures(1, &id);
}

void RmlUiRenderer::SetTransform(const Rml::Matrix4f* transform) {
    if (transform) {
        m_transform = glm::make_mat4(transform->data());
    } else {
        m_transform = glm::mat4(1.0f);
    }
}

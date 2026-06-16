#pragma once
#include "batch_renderer_2d.hpp"
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#else
#include <GLES3/gl3.h>
#endif
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class Renderer;

class RmlUiRenderer : public Rml::RenderInterface {
public:
    RmlUiRenderer(Renderer* renderer);
    ~RmlUiRenderer() override;

    Rml::CompiledGeometryHandle
      CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
      override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void SetTransform(const Rml::Matrix4f* transform) override;

    void Initialize();
    void Shutdown();

private:
    Renderer* m_Renderer;

    struct TextureData {
        GLuint id;
        int width;
        int height;
    };

    struct CompiledGeometry {
        std::vector<BatchVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct Scissor {
        bool enabled = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    // Texture management
    std::unordered_map<Rml::TextureHandle, TextureData> m_textures;
    Rml::TextureHandle m_next_texture_handle = 1;

    // Geometry management
    std::unordered_map<Rml::CompiledGeometryHandle, CompiledGeometry> m_geometry;
    Rml::CompiledGeometryHandle m_next_geometry_handle = 1;

    // State
    Scissor m_scissor;
    glm::mat4 m_transform = glm::mat4(1.0f);
};

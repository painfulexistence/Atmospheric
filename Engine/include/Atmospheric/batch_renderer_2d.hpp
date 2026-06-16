#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Forward declarations
class ShaderProgram;

// Blend modes for 2D rendering
enum class BlendMode {
    None,// No blending
    Alpha,// Standard alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    Additive,// Additive blending (SRC_ALPHA, ONE)
    Multiply,// Multiply blending (DST_COLOR, ZERO)
    Screen,// Screen blending (ONE, ONE_MINUS_SRC_COLOR)
    Premultiplied// Premultiplied alpha (ONE, ONE_MINUS_SRC_ALPHA)
};

struct BatchVertex {
    glm::vec3 position;
    glm::vec4 color;
    glm::vec2 uv;
    float texIndex;
    float entityID;// For picking or other identification
};

struct BatchDrawCommand {
    std::vector<BatchVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t textureID = 0;
    glm::mat4 transform = glm::mat4(1.0f);
};

struct BatchStats {
    uint32_t drawCalls = 0;
    uint32_t quadCount = 0;
    uint32_t lineCount = 0;
    uint32_t circleCount = 0;
    uint32_t triangleCount = 0;
};

// Forward declarations for friend classes
class CanvasPass;
class WorldCanvasPass;
class UIPass;

class BatchRenderer2D {
public:
    BatchRenderer2D();
    ~BatchRenderer2D();

    void Init();
    void Shutdown();

    // Flush current batch (use when changing blend mode mid-pass, etc.)
    void Flush();

    // Blend mode control
    void SetBlendMode(BlendMode mode);
    BlendMode GetBlendMode() const;

    // Primitives
    void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
    void DrawQuad(
      const glm::vec2& position, const glm::vec2& size, uint32_t textureID, const glm::vec4& color = glm::vec4(1.0f)
    );
    void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
    void DrawQuad(
      const glm::vec3& position, const glm::vec2& size, uint32_t textureID, const glm::vec4& color = glm::vec4(1.0f)
    );

    // Rotated Primitives
    void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
    void DrawRotatedQuad(
      const glm::vec2& position,
      const glm::vec2& size,
      float rotation,
      uint32_t textureID,
      const glm::vec4& color = glm::vec4(1.0f)
    );
    void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
    void DrawRotatedQuad(
      const glm::vec3& position,
      const glm::vec2& size,
      float rotation,
      uint32_t textureID,
      const glm::vec4& color = glm::vec4(1.0f)
    );

    // Advanced
    void DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
    void DrawQuad(
      const glm::mat4& transform, uint32_t textureID, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1
    );

    // With UVs (for spritesheets)
    void DrawQuad(
      const glm::mat4& transform,
      uint32_t textureID,
      const glm::vec2* texCoords,
      const glm::vec4& color = glm::vec4(1.0f),
      int entityID = -1
    );

    // ===== Shape Drawing API =====

    // Lines
    void DrawLine(const glm::vec2& p0, const glm::vec2& p1, const glm::vec4& color, float thickness = 1.0f);
    void DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, float thickness = 1.0f);

    // Circles
    void DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, int segments = 32);
    void DrawCircleFilled(const glm::vec2& center, float radius, const glm::vec4& color, int segments = 32);
    void DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 32);
    void DrawCircleFilled(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 32);

    // Triangles
    void DrawTriangle(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec4& color);
    void DrawTriangleFilled(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec4& color);
    void DrawTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color);
    void DrawTriangleFilled(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color);

    // Polygons (convex)
    void DrawPolygon(const std::vector<glm::vec2>& vertices, const glm::vec4& color, float thickness = 1.0f);
    void DrawPolygonFilled(const std::vector<glm::vec2>& vertices, const glm::vec4& color);
    void DrawPolygon(const std::vector<glm::vec3>& vertices, const glm::vec4& color, float thickness = 1.0f);
    void DrawPolygonFilled(const std::vector<glm::vec3>& vertices, const glm::vec4& color);

    // Rectangles (outline)
    void DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f);
    void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness = 1.0f);

    // Arbitrary Geometry (for RmlUi)
    void DrawGeometry(
      const std::vector<BatchVertex>& vertices,
      const std::vector<uint32_t>& indices,
      uint32_t textureID,
      const glm::mat4& transform
    );


    BatchStats GetStats();
    void ResetStats();

private:
    // Called by RenderPasses only (friend classes)
    void BeginBatch(const glm::mat4& viewProj, BlendMode blendMode = BlendMode::Alpha);
    void EndBatch();

    void StartBatch();
    void NextBatch();

    struct Renderer2DData;
    std::unique_ptr<Renderer2DData> m_Data;

    // Allow render passes to call BeginBatch/EndBatch
    friend class CanvasPass;
    friend class WorldCanvasPass;
    friend class UIPass;
};

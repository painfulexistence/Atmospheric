#include "batch_renderer_2d.hpp"
#include "asset_manager.hpp"
#include "console.hpp"
#include "globals.hpp" // provides glad on native, GLES3/gl3.h on Emscripten
#include "shader.hpp"
#include <array>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>

struct BatchRenderer2D::Renderer2DData {
    static const uint32_t MaxQuads = 20000;
    static const uint32_t MaxVertices = MaxQuads * 4;
    static const uint32_t MaxIndices = MaxQuads * 6;
    static const uint32_t MaxTextureSlots = 16;// Reduced to 16 to fit common OpenGL limits on macOS

    GLuint QuadVAO = 0;
    GLuint QuadVBO = 0;
    GLuint QuadIBO = 0;

    GLuint WhiteTexture = 0;
    uint32_t WhiteTextureSlot = 0;

    uint32_t QuadIndexCount = 0;
    BatchVertex* QuadVertexBufferBase = nullptr;
    BatchVertex* QuadVertexBufferPtr = nullptr;

    uint32_t* QuadIndexBufferBase = nullptr;
    uint32_t* QuadIndexBufferPtr = nullptr;

    std::array<uint32_t, MaxTextureSlots> TextureSlots;
    uint32_t TextureSlotIndex = 1;// 0 = white texture

    glm::vec4 QuadVertexPositions[4];
    glm::vec2 QuadTexCoords[4];// Default UVs

    BatchStats Stats;

    ShaderProgram* TextureShader = nullptr;

    // Blend mode state
    BlendMode CurrentBlendMode = BlendMode::Alpha;
};

BatchRenderer2D::BatchRenderer2D() {
    m_Data = std::make_unique<Renderer2DData>();
}

BatchRenderer2D::~BatchRenderer2D() {
    Shutdown();
}

void BatchRenderer2D::Init() {
    m_Data->QuadVertexBufferBase = new BatchVertex[m_Data->MaxVertices];

    glGenVertexArrays(1, &m_Data->QuadVAO);
    glBindVertexArray(m_Data->QuadVAO);

    glGenBuffers(1, &m_Data->QuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_Data->QuadVBO);
    glBufferData(GL_ARRAY_BUFFER, m_Data->MaxVertices * sizeof(BatchVertex), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (const void*)offsetof(BatchVertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (const void*)offsetof(BatchVertex, color));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (const void*)offsetof(BatchVertex, uv));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (const void*)offsetof(BatchVertex, texIndex));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (const void*)offsetof(BatchVertex, entityID));

    m_Data->QuadIndexBufferBase = new uint32_t[m_Data->MaxIndices];

    glGenBuffers(1, &m_Data->QuadIBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data->QuadIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Data->MaxIndices * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    // Create 1x1 white texture
    glGenTextures(1, &m_Data->WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_Data->WhiteTexture);
    uint32_t whiteTextureData = 0xffffffff;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteTextureData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_Data->TextureSlots[0] = m_Data->WhiteTexture;

    m_Data->QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
    m_Data->QuadVertexPositions[1] = { 0.5f, -0.5f, 0.0f, 1.0f };
    m_Data->QuadVertexPositions[2] = { 0.5f, 0.5f, 0.0f, 1.0f };
    m_Data->QuadVertexPositions[3] = { -0.5f, 0.5f, 0.0f, 1.0f };

    m_Data->QuadTexCoords[0] = { 0.0f, 0.0f };
    m_Data->QuadTexCoords[1] = { 1.0f, 0.0f };
    m_Data->QuadTexCoords[2] = { 1.0f, 1.0f };
    m_Data->QuadTexCoords[3] = { 0.0f, 1.0f };

    // Check shader
    try {
        auto shader = AssetManager::Get().GetShader("canvas");
        if (!shader) {
            Console::Get()->Error("BatchRenderer2D::Init: 'canvas' shader not found!");
        } else {
            Console::Get()->Info("BatchRenderer2D::Init: 'canvas' shader loaded successfully.");
        }
    } catch (const std::exception& e) {
        Console::Get()->Error(fmt::format("BatchRenderer2D::Init: Exception loading shader: {}", e.what()));
    }

    glBindVertexArray(0);// Unbind VAO to avoid state leakage
}

void BatchRenderer2D::Shutdown() {
    if (m_Data->QuadVAO) {
        glDeleteVertexArrays(1, &m_Data->QuadVAO);
        m_Data->QuadVAO = 0;
    }
    if (m_Data->QuadVBO) {
        glDeleteBuffers(1, &m_Data->QuadVBO);
        m_Data->QuadVBO = 0;
    }
    if (m_Data->QuadIBO) {
        glDeleteBuffers(1, &m_Data->QuadIBO);
        m_Data->QuadIBO = 0;
    }
    if (m_Data->WhiteTexture) {
        glDeleteTextures(1, &m_Data->WhiteTexture);
        m_Data->WhiteTexture = 0;
    }

    if (m_Data->QuadVertexBufferBase) {
        delete[] m_Data->QuadVertexBufferBase;
        m_Data->QuadVertexBufferBase = nullptr;
    }

    if (m_Data->QuadIndexBufferBase) {
        delete[] m_Data->QuadIndexBufferBase;
        m_Data->QuadIndexBufferBase = nullptr;
    }
}

void BatchRenderer2D::BeginBatch(const glm::mat4& viewProj, BlendMode blendMode) {
    m_Data->TextureShader = AssetManager::Get().GetShader("canvas");// Or "batch_2d"
    m_Data->TextureShader->Activate();

    // Check for errors after Activate
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        Console::Get()->Error(fmt::format("BatchRenderer2D::BeginScene (Activate): {}", err));
    }

    m_Data->TextureShader->SetUniform("Projection", viewProj);

    // Check for errors after SetUniform
    while ((err = glGetError()) != GL_NO_ERROR) {
        Console::Get()->Error(fmt::format("BatchRenderer2D::BeginScene (SetUniform): {}", err));
    }

    // Set blend mode
    SetBlendMode(blendMode);

    StartBatch();
}

void BatchRenderer2D::EndBatch() {
    Flush();
}

void BatchRenderer2D::StartBatch() {
    m_Data->QuadIndexCount = 0;
    m_Data->QuadVertexBufferPtr = m_Data->QuadVertexBufferBase;
    m_Data->QuadIndexBufferPtr = m_Data->QuadIndexBufferBase;
    m_Data->TextureSlotIndex = 1;
}

void BatchRenderer2D::Flush() {
    if (m_Data->QuadIndexCount == 0) return;

    uint32_t dataSize = (uint32_t)((uint8_t*)m_Data->QuadVertexBufferPtr - (uint8_t*)m_Data->QuadVertexBufferBase);
    glBindBuffer(GL_ARRAY_BUFFER, m_Data->QuadVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, m_Data->QuadVertexBufferBase);

    uint32_t indexDataSize = (uint32_t)((uint8_t*)m_Data->QuadIndexBufferPtr - (uint8_t*)m_Data->QuadIndexBufferBase);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data->QuadIBO);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indexDataSize, m_Data->QuadIndexBufferBase);

    // Apply blend mode
    if (m_Data->CurrentBlendMode == BlendMode::None) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        switch (m_Data->CurrentBlendMode) {
        case BlendMode::Alpha:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiply:
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case BlendMode::Screen:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case BlendMode::Premultiplied:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    }

    // Bind textures
    for (uint32_t i = 0; i < m_Data->TextureSlotIndex; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_Data->TextureSlots[i]);
        // Update shader uniform if needed (if using individual uniforms)
        m_Data->TextureShader->SetUniform(fmt::format("Textures[{}]", i), (int)i);
    }

    glBindVertexArray(m_Data->QuadVAO);
    glDrawElements(GL_TRIANGLES, m_Data->QuadIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Check for errors
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        Console::Get()->Error(fmt::format("BatchRenderer2D::Flush: {}", err));
    }

    m_Data->Stats.drawCalls++;
}

void BatchRenderer2D::NextBatch() {
    Flush();
    StartBatch();
}

void BatchRenderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
    DrawQuad({ position.x, position.y, 0.0f }, size, color);
}

void BatchRenderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color) {
    glm::mat4 transform =
      glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

    DrawQuad(transform, m_Data->WhiteTexture, color);
}

void BatchRenderer2D::DrawQuad(
  const glm::vec2& position, const glm::vec2& size, uint32_t textureID, const glm::vec4& color
) {
    DrawQuad({ position.x, position.y, 0.0f }, size, textureID, color);
}

void BatchRenderer2D::DrawQuad(
  const glm::vec3& position, const glm::vec2& size, uint32_t textureID, const glm::vec4& color
) {
    glm::mat4 transform =
      glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

    DrawQuad(transform, textureID, color);
}

void BatchRenderer2D::DrawRotatedQuad(
  const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color
) {
    DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
}

void BatchRenderer2D::DrawRotatedQuad(
  const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color
) {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
                          * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
                          * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

    DrawQuad(transform, m_Data->WhiteTexture, color);
}

void BatchRenderer2D::DrawRotatedQuad(
  const glm::vec2& position, const glm::vec2& size, float rotation, uint32_t textureID, const glm::vec4& color
) {
    DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, textureID, color);
}

void BatchRenderer2D::DrawRotatedQuad(
  const glm::vec3& position, const glm::vec2& size, float rotation, uint32_t textureID, const glm::vec4& color
) {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
                          * glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f })
                          * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

    DrawQuad(transform, textureID, color);
}

void BatchRenderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID) {
    DrawQuad(transform, m_Data->WhiteTexture, color, entityID);
}

void BatchRenderer2D::DrawQuad(const glm::mat4& transform, uint32_t textureID, const glm::vec4& color, int entityID) {
    DrawQuad(transform, textureID, m_Data->QuadTexCoords, color, entityID);
}

void BatchRenderer2D::DrawQuad(
  const glm::mat4& transform, uint32_t textureID, const glm::vec2* texCoords, const glm::vec4& color, int entityID
) {
    if (m_Data->QuadIndexCount >= Renderer2DData::MaxIndices) NextBatch();

    if (textureID == (uint32_t)-1 || textureID == 0) textureID = m_Data->WhiteTexture;

    float textureIndex = 0.0f;
    if (textureID != m_Data->WhiteTexture) {
        for (uint32_t i = 1; i < m_Data->TextureSlotIndex; i++) {
            if (m_Data->TextureSlots[i] == textureID) {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == 0.0f) {
            if (m_Data->TextureSlotIndex >= Renderer2DData::MaxTextureSlots) NextBatch();

            textureIndex = (float)m_Data->TextureSlotIndex;
            m_Data->TextureSlots[m_Data->TextureSlotIndex] = textureID;
            m_Data->TextureSlotIndex++;
        }
    }

    uint32_t vertexOffset = (uint32_t)(m_Data->QuadVertexBufferPtr - m_Data->QuadVertexBufferBase);

    // Vertex population (existing code)
    for (size_t i = 0; i < 4; i++) {
        m_Data->QuadVertexBufferPtr->position = transform * m_Data->QuadVertexPositions[i];
        m_Data->QuadVertexBufferPtr->color = color;
        m_Data->QuadVertexBufferPtr->uv = texCoords[i];
        m_Data->QuadVertexBufferPtr->texIndex = textureIndex;
        m_Data->QuadVertexBufferPtr->entityID = (float)entityID;
        m_Data->QuadVertexBufferPtr++;
    }

    // Index population
    m_Data->QuadIndexBufferPtr[0] = vertexOffset + 0;
    m_Data->QuadIndexBufferPtr[1] = vertexOffset + 1;
    m_Data->QuadIndexBufferPtr[2] = vertexOffset + 2;

    m_Data->QuadIndexBufferPtr[3] = vertexOffset + 2;
    m_Data->QuadIndexBufferPtr[4] = vertexOffset + 3;
    m_Data->QuadIndexBufferPtr[5] = vertexOffset + 0;

    m_Data->QuadIndexBufferPtr += 6;

    m_Data->QuadIndexCount += 6;
    m_Data->Stats.quadCount++;
}

// ===== Shape Drawing Implementation =====

void BatchRenderer2D::DrawLine(const glm::vec2& p0, const glm::vec2& p1, const glm::vec4& color, float thickness) {
    DrawLine(glm::vec3(p0, 0.0f), glm::vec3(p1, 0.0f), color, thickness);
}

void BatchRenderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, float thickness) {
    // Create a quad along the line with the specified thickness
    glm::vec3 direction = p1 - p0;
    float length = glm::length(glm::vec2(direction));
    if (length < 0.0001f) return;

    glm::vec3 normalized = direction / length;
    glm::vec3 perpendicular(-normalized.y, normalized.x, 0.0f);
    float halfThickness = thickness * 0.5f;

    // Four corners of the line quad
    glm::vec3 v0 = p0 - perpendicular * halfThickness;
    glm::vec3 v1 = p1 - perpendicular * halfThickness;
    glm::vec3 v2 = p1 + perpendicular * halfThickness;
    glm::vec3 v3 = p0 + perpendicular * halfThickness;

    if (m_Data->QuadIndexCount >= Renderer2DData::MaxIndices) NextBatch();

    glm::vec2 defaultUV(0.5f, 0.5f);// Center of white texture

    m_Data->QuadVertexBufferPtr->position = v0;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    m_Data->QuadVertexBufferPtr->position = v1;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    m_Data->QuadVertexBufferPtr->position = v2;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    m_Data->QuadVertexBufferPtr->position = v3;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    // Index population
    uint32_t vertexOffset = (uint32_t)(m_Data->QuadVertexBufferPtr - m_Data->QuadVertexBufferBase) - 4;
    m_Data->QuadIndexBufferPtr[0] = vertexOffset + 0;
    m_Data->QuadIndexBufferPtr[1] = vertexOffset + 1;
    m_Data->QuadIndexBufferPtr[2] = vertexOffset + 2;

    m_Data->QuadIndexBufferPtr[3] = vertexOffset + 2;
    m_Data->QuadIndexBufferPtr[4] = vertexOffset + 3;
    m_Data->QuadIndexBufferPtr[5] = vertexOffset + 0;

    m_Data->QuadIndexBufferPtr += 6;

    m_Data->QuadIndexCount += 6;
    m_Data->Stats.lineCount++;
}

void BatchRenderer2D::DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, int segments) {
    DrawCircle(glm::vec3(center, 0.0f), radius, color, segments);
}

void BatchRenderer2D::DrawCircle(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
    float angleStep = 2.0f * glm::pi<float>() / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float angle0 = angleStep * i;
        float angle1 = angleStep * (i + 1);

        glm::vec3 p0 = center + glm::vec3(std::cos(angle0) * radius, std::sin(angle0) * radius, 0.0f);
        glm::vec3 p1 = center + glm::vec3(std::cos(angle1) * radius, std::sin(angle1) * radius, 0.0f);

        DrawLine(p0, p1, color, 1.0f);
    }
    m_Data->Stats.circleCount++;
}

void BatchRenderer2D::DrawCircleFilled(const glm::vec2& center, float radius, const glm::vec4& color, int segments) {
    DrawCircleFilled(glm::vec3(center, 0.0f), radius, color, segments);
}

void BatchRenderer2D::DrawCircleFilled(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
    // Draw as triangle fan using multiple triangles
    float angleStep = 2.0f * glm::pi<float>() / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float angle0 = angleStep * i;
        float angle1 = angleStep * (i + 1);

        glm::vec3 p0 = center;
        glm::vec3 p1 = center + glm::vec3(std::cos(angle0) * radius, std::sin(angle0) * radius, 0.0f);
        glm::vec3 p2 = center + glm::vec3(std::cos(angle1) * radius, std::sin(angle1) * radius, 0.0f);

        DrawTriangleFilled(p0, p1, p2, color);
    }
    m_Data->Stats.circleCount++;
}

void BatchRenderer2D::DrawTriangle(
  const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec4& color
) {
    DrawTriangle(glm::vec3(p0, 0.0f), glm::vec3(p1, 0.0f), glm::vec3(p2, 0.0f), color);
}

void BatchRenderer2D::DrawTriangle(
  const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color
) {
    DrawLine(p0, p1, color, 1.0f);
    DrawLine(p1, p2, color, 1.0f);
    DrawLine(p2, p0, color, 1.0f);
}

void BatchRenderer2D::DrawTriangleFilled(
  const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec4& color
) {
    DrawTriangleFilled(glm::vec3(p0, 0.0f), glm::vec3(p1, 0.0f), glm::vec3(p2, 0.0f), color);
}

void BatchRenderer2D::DrawTriangleFilled(
  const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color
) {
    // For triangles, we need to use 2 triangles forming a degenerate quad
    // We'll use the same vertex layout but with careful positioning
    if (m_Data->QuadIndexCount >= Renderer2DData::MaxIndices) NextBatch();

    glm::vec2 defaultUV(0.5f, 0.5f);

    // Vertex 0
    m_Data->QuadVertexBufferPtr->position = p0;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    // Vertex 1
    m_Data->QuadVertexBufferPtr->position = p1;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    // Vertex 2
    m_Data->QuadVertexBufferPtr->position = p2;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    // Vertex 3 (duplicate of vertex 2 to complete the quad - degenerate triangle)
    m_Data->QuadVertexBufferPtr->position = p2;
    m_Data->QuadVertexBufferPtr->color = color;
    m_Data->QuadVertexBufferPtr->uv = defaultUV;
    m_Data->QuadVertexBufferPtr->texIndex = 0.0f;
    m_Data->QuadVertexBufferPtr->entityID = -1.0f;
    m_Data->QuadVertexBufferPtr++;

    // Index population
    uint32_t vertexOffset = (uint32_t)(m_Data->QuadVertexBufferPtr - m_Data->QuadVertexBufferBase) - 4;
    m_Data->QuadIndexBufferPtr[0] = vertexOffset + 0;
    m_Data->QuadIndexBufferPtr[1] = vertexOffset + 1;
    m_Data->QuadIndexBufferPtr[2] = vertexOffset + 2;

    m_Data->QuadIndexBufferPtr[3] = vertexOffset + 2;
    m_Data->QuadIndexBufferPtr[4] = vertexOffset + 3;
    m_Data->QuadIndexBufferPtr[5] = vertexOffset + 0;

    m_Data->QuadIndexBufferPtr += 6;

    m_Data->QuadIndexCount += 6;
    m_Data->Stats.triangleCount++;
}

void BatchRenderer2D::DrawPolygon(const std::vector<glm::vec2>& vertices, const glm::vec4& color, float thickness) {
    std::vector<glm::vec3> vertices3D;
    vertices3D.reserve(vertices.size());
    for (const auto& v : vertices) {
        vertices3D.push_back(glm::vec3(v, 0.0f));
    }
    DrawPolygon(vertices3D, color, thickness);
}

void BatchRenderer2D::DrawPolygon(const std::vector<glm::vec3>& vertices, const glm::vec4& color, float thickness) {
    if (vertices.size() < 3) return;

    for (size_t i = 0; i < vertices.size(); ++i) {
        size_t next = (i + 1) % vertices.size();
        DrawLine(vertices[i], vertices[next], color, thickness);
    }
}

void BatchRenderer2D::DrawPolygonFilled(const std::vector<glm::vec2>& vertices, const glm::vec4& color) {
    std::vector<glm::vec3> vertices3D;
    vertices3D.reserve(vertices.size());
    for (const auto& v : vertices) {
        vertices3D.push_back(glm::vec3(v, 0.0f));
    }
    DrawPolygonFilled(vertices3D, color);
}

void BatchRenderer2D::DrawPolygonFilled(const std::vector<glm::vec3>& vertices, const glm::vec4& color) {
    if (vertices.size() < 3) return;

    // Simple triangle fan for convex polygons
    for (size_t i = 1; i < vertices.size() - 1; ++i) {
        DrawTriangleFilled(vertices[0], vertices[i], vertices[i + 1], color);
    }
}

void BatchRenderer2D::DrawRect(
  const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float thickness
) {
    DrawRect(glm::vec3(position, 0.0f), size, color, thickness);
}

void BatchRenderer2D::DrawRect(
  const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float thickness
) {
    glm::vec3 topLeft = position;
    glm::vec3 topRight = position + glm::vec3(size.x, 0.0f, 0.0f);
    glm::vec3 bottomRight = position + glm::vec3(size.x, size.y, 0.0f);
    glm::vec3 bottomLeft = position + glm::vec3(0.0f, size.y, 0.0f);

    DrawLine(topLeft, topRight, color, thickness);
    DrawLine(topRight, bottomRight, color, thickness);
    DrawLine(bottomRight, bottomLeft, color, thickness);
    DrawLine(bottomLeft, topLeft, color, thickness);
}

void BatchRenderer2D::DrawGeometry(
  const std::vector<BatchVertex>& vertices,
  const std::vector<uint32_t>& indices,
  uint32_t textureID,
  const glm::mat4& transform
) {
    if (vertices.empty() || indices.empty()) return;

    // Check if we have enough space
    if (m_Data->QuadIndexCount + indices.size() >= Renderer2DData::MaxIndices
    || (m_Data->QuadVertexBufferPtr - m_Data->QuadVertexBufferBase) + vertices.size() >= Renderer2DData::MaxVertices) {
        NextBatch();
    }

    // Handle texture
    if (textureID == (uint32_t)-1 || textureID == 0) textureID = m_Data->WhiteTexture;

    float textureIndex = 0.0f;
    if (textureID != m_Data->WhiteTexture) {
        for (uint32_t i = 1; i < m_Data->TextureSlotIndex; i++) {
            if (m_Data->TextureSlots[i] == textureID) {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == 0.0f) {
            if (m_Data->TextureSlotIndex >= Renderer2DData::MaxTextureSlots) NextBatch();

            textureIndex = (float)m_Data->TextureSlotIndex;
            m_Data->TextureSlots[m_Data->TextureSlotIndex] = textureID;
            m_Data->TextureSlotIndex++;
        }
    }

    // Current vertex count (offset for indices)
    uint32_t vertexOffset = (uint32_t)(m_Data->QuadVertexBufferPtr - m_Data->QuadVertexBufferBase);

    // Copy vertices
    for (const auto& vertex : vertices) {
        m_Data->QuadVertexBufferPtr->position = transform * glm::vec4(vertex.position, 1.0f);
        m_Data->QuadVertexBufferPtr->color = vertex.color;
        m_Data->QuadVertexBufferPtr->uv = vertex.uv;
        m_Data->QuadVertexBufferPtr->texIndex = textureIndex;
        m_Data->QuadVertexBufferPtr->entityID = vertex.entityID;
        m_Data->QuadVertexBufferPtr++;
    }

    // Copy indices (with offset)
    for (uint32_t index : indices) {
        *m_Data->QuadIndexBufferPtr = vertexOffset + index;
        m_Data->QuadIndexBufferPtr++;
    }

    m_Data->QuadIndexCount += (uint32_t)indices.size();
    m_Data->Stats.quadCount += (uint32_t)indices.size() / 6;// Approx
}

BatchStats BatchRenderer2D::GetStats() {
    return m_Data->Stats;
}

void BatchRenderer2D::ResetStats() {
    memset(&m_Data->Stats, 0, sizeof(BatchStats));
}

void BatchRenderer2D::SetBlendMode(BlendMode mode) {
    if (m_Data->CurrentBlendMode != mode) {
        // Flush current batch before changing blend mode
        if (m_Data->QuadIndexCount > 0) {
            Flush();
            StartBatch();
        }
        m_Data->CurrentBlendMode = mode;
    }
}

BlendMode BatchRenderer2D::GetBlendMode() const {
    return m_Data->CurrentBlendMode;
}

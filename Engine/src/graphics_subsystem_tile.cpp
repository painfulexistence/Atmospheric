// DrawTile / DrawSprite2D implementation — added as a separate translation
// unit to avoid bloating graphics_subsystem.cpp.
// Ported from 2d-engine/src/graphics/tilemap.ts

#include "batch_renderer_2d.hpp"
#include "graphics_subsystem.hpp"
#include "renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>

// Build a BatchDrawCommand for a quad with per-vertex UV coordinates.
// cx, cy — CENTER of the quad in screen pixels.
void GraphicsSubsystem::SubmitUVQuad(
    float cx,
    float cy,
    float w,
    float h,
    uint32_t texID,
    const glm::vec2& uvMin,
    const glm::vec2& uvMax,
    const glm::vec4& color
) {
    // Corners in counter-clockwise order to match the existing CreateQuad helper
    // used by DrawTexturedQuad (which centers at the translated position).
    glm::vec2 uvs[4] = {
        { uvMin.x, uvMin.y },// bottom-left
        { uvMax.x, uvMin.y },// bottom-right
        { uvMax.x, uvMax.y },// top-right
        { uvMin.x, uvMax.y },// top-left
    };

    BatchDrawCommand cmd;
    cmd.textureID = texID;
    cmd.transform = glm::mat4(1.0f);

    // Build vertices manually — mirrors graphics_subsystem.cpp::CreateQuad layout
    // Positions: (-0.5,-0.5)…(0.5,0.5) in local space, scaled & translated.
    glm::vec4 localCorners[4] = {
        { -0.5f, -0.5f, 0.0f, 1.0f },
        { 0.5f, -0.5f, 0.0f, 1.0f },
        { 0.5f, 0.5f, 0.0f, 1.0f },
        { -0.5f, 0.5f, 0.0f, 1.0f },
    };

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f));
    transform = glm::scale(transform, glm::vec3(w, h, 1.0f));

    auto startIdx = static_cast<uint32_t>(cmd.vertices.size());
    for (int i = 0; i < 4; i++) {
        BatchVertex v;
        v.position = glm::vec3(transform * localCorners[i]);
        v.color = color;
        v.uv = uvs[i];
        v.texIndex = 0.0f;
        v.entityID = -1.0f;
        cmd.vertices.push_back(v);
    }
    cmd.indices = { startIdx + 0, startIdx + 1, startIdx + 2, startIdx + 2, startIdx + 3, startIdx + 0 };

    renderer->SubmitCanvasCommand(cmd);
}

void GraphicsSubsystem::DrawTile(
    float x,
    float y,
    float w,
    float h,
    uint32_t texID,
    const glm::vec2& tilesetDims,
    int tileCol,
    int tileRow,
    const glm::vec4& color
) {
    // Convert top-left origin to center origin expected by SubmitUVQuad
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    glm::vec2 uvMin = glm::vec2(static_cast<float>(tileCol), static_cast<float>(tileRow)) / tilesetDims;
    glm::vec2 uvMax = glm::vec2(static_cast<float>(tileCol + 1), static_cast<float>(tileRow + 1)) / tilesetDims;

    SubmitUVQuad(cx, cy, w, h, texID, uvMin, uvMax, color);
}

void GraphicsSubsystem::DrawSprite2D(
    float x,
    float y,
    float w,
    float h,
    uint32_t texID,
    const glm::vec2& uvMin,
    const glm::vec2& uvMax,
    const glm::vec4& color
) {
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    SubmitUVQuad(cx, cy, w, h, texID, uvMin, uvMax, color);
}

#pragma once
#include "camera_component.hpp"
#include "config.hpp"
#include "font_manager.hpp"
#include "light_component.hpp"
#include "mesh.hpp"
#include "mesh_component.hpp"
#include "buffer.hpp"
#include "render_target.hpp"
#include "sun_component.hpp"
#include "vertex.hpp"
#include "server.hpp"
#include "shader.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <stack>
#include <unordered_map>

class SpriteComponent;

enum class DrawMode { Static, Dynamic, Stream };

struct CanvasVertex {
    glm::vec2 position;
    glm::vec2 texCoord;
    glm::vec4 color;
    int texIndex;
    CanvasLayer layer;
};

struct CameraData {
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

struct InstanceData {
    glm::mat4 modelMatrix;
};

class Renderer;

class MeshComponent;
class CanvasDrawable;
class SpriteComponent;
class CameraComponent;
class LightComponent;
class BatchRenderer2D;

class GraphicsServer : public Server {
private:
    static GraphicsServer* _instance;

public:
    static GraphicsServer* Get() {
        return _instance;
    }
    std::vector<GLuint> canvasTextures;
    std::vector<MeshComponent*> renderables;
    std::vector<CanvasDrawable*> canvasDrawables;
    std::vector<LightComponent*> directionalLights;
    std::vector<LightComponent*> pointLights;
    std::vector<CameraComponent*> cameras;
    std::vector<SunComponent*> sunComponents;

    std::vector<DebugVertex> debugLines;
    std::vector<CanvasVertex> canvasDrawList;
    int _debugLineCount  = 0;
    int _canvasQuadCount = 0;

    Mesh* debugLineMesh = nullptr;
    Mesh* canvasMesh    = nullptr;
    ShaderProgram* debugShader  = nullptr;
    ShaderProgram* canvasShader = nullptr;

    GraphicsServer();
    ~GraphicsServer();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    void Reset();
    void Render(CameraComponent* camera, float dt);

    void PushDebugLine(DebugVertex from, DebugVertex to) {
        debugLines.push_back(from);
        debugLines.push_back(to);
    }

    CameraComponent* GetMainCamera() const {
        return cameras.size() > 0 ? cameras[0] : defaultCamera;
    }

    LightComponent* GetMainLight() const {
        return directionalLights.size() > 0 ? directionalLights[0] : defaultLight;
    }

    SunComponent* GetMainSun() const {
        return sunComponents.empty() ? nullptr : sunComponents[0];
    }

    ShaderProgram* GetShader(const std::string& name) const;
    ShaderProgram* GetShaderByID(uint32_t id) const;
    Mesh* GetMesh(const std::string& name) const;

    MeshComponent*   RegisterMesh(MeshComponent* mesh);
    CameraComponent* RegisterCamera(CameraComponent* camera);
    LightComponent*  RegisterLight(LightComponent* light);
    SunComponent*    RegisterSun(SunComponent* sun);
    CanvasDrawable*  RegisterCanvasDrawable(CanvasDrawable* drawable);

    // ===== Render Target Management =====

    std::shared_ptr<RenderTarget> CreateRenderTarget(int width, int height, bool withDepth = false);
    std::shared_ptr<RenderTarget> CreateRenderTarget(const RenderTarget::Props& props);

    void PushRenderTarget(RenderTarget* target);
    void PopRenderTarget();
    void SetRenderTarget(RenderTarget* target);
    RenderTarget* GetCurrentRenderTarget() const;

    size_t GetRenderTargetStackDepth() const {
        return _renderTargetStack.size();
    }

    // GLBuffer (vertex/index buffer) management
    RenderMeshHandle AllocateRenderMesh(VertexFormat format, BufferUsage usage = BufferUsage::Static);
    void FreeRenderMesh(RenderMeshHandle handle);
    Buffer* GetRenderMesh(RenderMeshHandle handle);

    // ===== 2D Rendering (Queued for UI) =====
    void DrawQuad(float x, float y, float w, float h, float rotation, const glm::vec4& color);
    void DrawTexturedQuad(float x, float y, float w, float h, float rotation,
                          uint32_t textureID, const glm::vec4& color);
    void DrawRect(float x, float y, float w, float h, const glm::vec4& color);
    void DrawLine(float x1, float y1, float x2, float y2, const glm::vec4& color);
    void DrawCircle(float x, float y, float radius, const glm::vec4& color);

    /// Draw a single tile from a tileset spritesheet.
    /// @param x,y    Top-left world pixel position of the tile.
    /// @param w,h    Tile size in pixels (usually tileSize x tileSize).
    /// @param texID  OpenGL texture ID of the tileset image.
    /// @param tilesetDims  vec2(numCols, numRows) of the tileset grid.
    /// @param tileCol,tileRow  Zero-based column and row of the desired tile.
    void DrawTile(float x, float y, float w, float h,
                  uint32_t texID,
                  const glm::vec2& tilesetDims,
                  int tileCol, int tileRow,
                  const glm::vec4& color = glm::vec4(1.0f));

    /// Draw a sprite from a spritesheet with explicit UV coordinates.
    /// @param x,y     Top-left screen pixel position.
    /// @param w,h     Display size in pixels.
    /// @param texID   OpenGL texture ID.
    /// @param uvMin   Bottom-left UV (0-1)
    /// @param uvMax   Top-right   UV (0-1)
    void DrawSprite2D(float x, float y, float w, float h,
                      uint32_t texID,
                      const glm::vec2& uvMin,
                      const glm::vec2& uvMax,
                      const glm::vec4& color = glm::vec4(1.0f));

    // ===== Text Rendering =====
    FontID LoadFont(const std::string& path, float baseSize);
    void UnloadFont(FontID id);
    void DrawText(FontID fontID, const std::string& text, float x, float y,
                  float scale, const glm::vec4& color);
    void DrawText3D(FontID fontID, const std::string& text, glm::vec3 position,
                    float scale, const glm::vec4& color);
    glm::vec2 MeasureText(FontID fontID, const std::string& text, float scale = 1.0f);
    float GetFontLineHeight(FontID fontID, float scale = 1.0f);

    Renderer* renderer = nullptr;

private:
    std::stack<RenderTarget*> _renderTargetStack;
    RenderTarget* _currentRenderTarget = nullptr;

    std::vector<std::shared_ptr<RenderTarget>> _renderTargets;
    CameraComponent* defaultCamera = nullptr;
    LightComponent*  defaultLight  = nullptr;

    std::unordered_map<uint32_t, std::unique_ptr<Buffer>> _renderMeshes;
    uint32_t _nextRenderMeshId = 0;

    FontManager _fontManager;

    static constexpr int MAX_CANVAS_TEXTURES = 32;

    void PushCanvasQuad(
      float x, float y, float w, float h,
      float angle, float pivotX, float pivotY,
      const glm::vec4& color, int texIndex,
      CanvasLayer layer = CanvasLayer::LAYER_WORLD_2D,
      const glm::vec2& uvMin = glm::vec2(0.0f),
      const glm::vec2& uvMax = glm::vec2(1.0f));
    void PushCanvasQuadTiled(
      float x, float y, float w, float h,
      float angle, float pivotX, float pivotY,
      const glm::vec4& color, int texIndex,
      CanvasLayer layer = CanvasLayer::LAYER_WORLD_2D,
      const glm::vec2& tilesetSize = glm::vec2(1.0f),
      const glm::vec2& tileIndex   = glm::vec2(0.0f));

    // Helper: build UV-mapped BatchDrawCommand and submit to canvas queue.
    void SubmitUVQuad(float cx, float cy, float w, float h,
                      uint32_t texID,
                      const glm::vec2& uvMin, const glm::vec2& uvMax,
                      const glm::vec4& color);

    struct TextCommand {
        FontID fontID;
        std::string text;
        float x, y, scale;
        glm::vec4 color;
    };
    std::vector<TextCommand> _textCommands;

public:
    void RenderBufferedText(BatchRenderer2D* batch);
    // WebGPU path: converts buffered text commands into BatchDrawCommands
    // and submits them via SubmitCanvasCommand so CanvasPass can process them.
    void FlushTextToQueue();
};

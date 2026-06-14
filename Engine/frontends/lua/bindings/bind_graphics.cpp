#include "../lua_application.hpp"
#include "Atmospheric/graphics_server.hpp"
#include "Atmospheric/renderer.hpp"

// Keep track of current draw color
static glm::vec4 s_CurrentColor = glm::vec4(1.0f);

void BindGraphicsAPI(sol::state& lua, GraphicsServer* graphics) {
    sol::table atmos = lua["atmos"];
    sol::table gfx = atmos.create("graphics");

    // Get main camera
    gfx["getMainCamera"] = [graphics]() -> CameraComponent* { return graphics->GetMainCamera(); };

    // Get main light
    gfx["getMainLight"] = [graphics]() -> LightComponent* { return graphics->GetMainLight(); };

    // Screen dimensions (via Window)
    gfx["getScreenSize"] = []() -> std::tuple<int, int> {
        auto size = Window::Get()->GetSize();
        return std::make_tuple(size.width, size.height);
    };

    gfx["getWidth"] = []() { return Window::Get()->GetSize().width; };

    gfx["getHeight"] = []() { return Window::Get()->GetSize().height; };

    // Shader management
    gfx["reloadShaders"] = []() { AssetManager::Get().ReloadShaders(); };

    // ===== 2D Immediate Mode Drawing =====
    // Note: These use the BatchRenderer2D and should be called within proper scene bounds

    // Set current draw color
    gfx["setColor"] = sol::overload(
      [](float r, float g, float b) { s_CurrentColor = glm::vec4(r, g, b, 1.0f); },
      [](float r, float g, float b, float a) { s_CurrentColor = glm::vec4(r, g, b, a); },
      [](const glm::vec4& color) { s_CurrentColor = color; }
    );

    gfx["getColor"] = []() -> glm::vec4 { return s_CurrentColor; };

    // Draw line
    gfx["drawLine"] = [graphics](float x1, float y1, float x2, float y2) {
        graphics->DrawLine(x1, y1, x2, y2, s_CurrentColor);
    };

    // Draw circle (outline)
    gfx["drawCircle"] = [graphics](float x, float y, float radius) {
        graphics->DrawCircle(x, y, radius, s_CurrentColor);
    };

    gfx["drawRect"] = sol::overload(
      [graphics](float x, float y, float w, float h) { graphics->DrawQuad(x, y, w, h, 0.0f, s_CurrentColor); },
      [graphics](float x, float y, float w, float h, float rotation) {
          graphics->DrawQuad(x, y, w, h, rotation, s_CurrentColor);
      }
    );

    // I should probably add drawRectangleOutline for outline?
    gfx["drawRectangle"] = [graphics](float x, float y, float w, float h) {
        graphics->DrawRect(x, y, w, h, s_CurrentColor);
    };

    // Draw textured sprite
    // Draw textured sprite
    gfx["drawSprite"] = sol::overload(
      // Draw with texture ID at position
      [graphics](uint32_t texID, float x, float y) {
          // Assume 64x64 default size. DrawTexturedQuad expects center position.
          graphics->DrawTexturedQuad(x, y, 64.0f, 64.0f, 0.0f, texID, s_CurrentColor);
      },
      // Draw with texture ID, position, and size
      [graphics](uint32_t texID, float x, float y, float w, float h) {
          graphics->DrawTexturedQuad(x, y, w, h, 0.0f, texID, s_CurrentColor);
      },
      // Draw with texture ID, position, size, and rotation
      [graphics](uint32_t texID, float x, float y, float w, float h, float rotation) {
          graphics->DrawTexturedQuad(x, y, w, h, rotation, texID, s_CurrentColor);
      }
    );

    // ===== Text Rendering =====


    // Load a font (returns FontID)
    gfx["loadFont"] = [graphics](const std::string& path, float size) -> FontID {
        return graphics->LoadFont(path, size);
    };

    // Unload a font
    gfx["unloadFont"] = [graphics](FontID id) { graphics->UnloadFont(id); };

    // Draw text with current color
    // drawText(font, text, x, y)           -- uses base font size
    // drawText(font, text, x, y, scale)    -- scale relative to base size
    gfx["drawText"] = sol::overload(
      [graphics](FontID fontID, const std::string& text, float x, float y) {
          graphics->DrawText(fontID, text, x, y, 1.0f, s_CurrentColor);
      },
      [graphics](FontID fontID, const std::string& text, float x, float y, float scale) {
          graphics->DrawText(fontID, text, x, y, scale, s_CurrentColor);
      }
    );

    // Measure text dimensions
    gfx["measureText"] = sol::overload(
      [graphics](FontID fontID, const std::string& text) -> std::tuple<float, float> {
          glm::vec2 size = graphics->MeasureText(fontID, text, 1.0f);
          return std::make_tuple(size.x, size.y);
      },
      [graphics](FontID fontID, const std::string& text, float scale) -> std::tuple<float, float> {
          glm::vec2 size = graphics->MeasureText(fontID, text, scale);
          return std::make_tuple(size.x, size.y);
      }
    );

    // Get font line height
    gfx["getFontHeight"] = [graphics](FontID fontID, float scale) -> float {
        return graphics->GetFontLineHeight(fontID, scale);
    };


    // ===== CameraComponent usertype =====
    lua.new_usertype<CameraComponent>(
      "CameraComponent",
      sol::no_constructor,

      "gameObject",
      sol::readonly(&CameraComponent::gameObject)
    );

    // ===== LightComponent usertype =====
    lua.new_usertype<LightComponent>(
      "LightComponent",
      sol::no_constructor,

      "gameObject",
      sol::readonly(&LightComponent::gameObject),

      // Direct access to public members
      "intensity",
      &LightComponent::intensity,
      "ambient",
      &LightComponent::ambient,
      "diffuse",
      &LightComponent::diffuse,
      "specular",
      &LightComponent::specular,
      "castShadow",
      &LightComponent::castShadow
    );

    // ===== Asset management =====
    sol::table assets = atmos.create("assets");

    assets["createCubeMesh"] = [](const std::string& name, float size) -> Mesh* {
        return AssetManager::Get().CreateCubeMesh(name, size);
    };

    assets["createSphereMesh"] = [](const std::string& name, float radius, int segments) -> Mesh* {
        return AssetManager::Get().CreateSphereMesh(name, radius, segments);
    };

    assets["createPlaneMesh"] = [](const std::string& name, float width, float height) -> Mesh* {
        return AssetManager::Get().CreatePlaneMesh(name, width, height);
    };

    assets["getMesh"] = [](const std::string& name) -> Mesh* { return AssetManager::Get().GetMesh(name); };

    assets["loadTexture"] = [](const std::string& path) { AssetManager::Get().LoadTextures({ path }); };

    // Get texture by name (returns GL texture ID for use with drawSprite)
    assets["getTexture"] = [](const std::string& name) -> uint32_t { return AssetManager::Get().GetTexture(name); };

    // ===== Mesh usertype =====
    lua.new_usertype<Mesh>(
      "Mesh",
      sol::no_constructor,

      "setMaterial",
      [](Mesh* mesh, int index) {
          auto& materials = AssetManager::Get().GetMaterials();
          if (index >= 0 && index < static_cast<int>(materials.size())) {
              mesh->SetMaterial(materials[index]);
          }
      }
    );
}

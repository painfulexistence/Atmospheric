#include "game_layer.hpp"
#include "application.hpp"
#include "game_object.hpp"
#include "mesh_component.hpp"
#include "rigidbody_component.hpp"
#include "rmlui_manager.hpp"
#include "ui_page_manager.hpp"
#include "window.hpp"

GameLayer::GameLayer(Application* app) : Layer("GameLayer"), _app(app) {
}

void GameLayer::OnUpdate(float dt) {
    for (auto& entity : _app->GetEntities()) {
        entity->Tick(dt);
    }

    RmlUiManager::Get()->Update(dt);
    UIPageManager::Get()->Update(dt);
}

void GameLayer::OnRender(float dt) {
    // Note that draw calls are asynchronous, which means they return immediately.
    // So the drawing time can only be calculated along with the image presenting.
    // Generate UI commands
    RmlUiManager::Get()->Render();

    // Render the frame (executes all passes including UI)
    _app->GetGraphicsServer()->Render(_app->GetMainCamera(), dt);
    // Nevertheless, glFinish() can force the GPU process all the commands synchronously.
    // glFinish();
}

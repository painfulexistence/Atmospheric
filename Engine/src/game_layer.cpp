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
    // Index loop, size re-read every iteration: component OnTick handlers may
    // CreateGameObject (streamed tiles/entities/grass spawn mid-tick), which
    // push_back into this vector — a range-for's cached iterators dangle when
    // that push_back reallocates. Entities appended mid-loop get their first
    // Tick this same frame. Removing entities during the loop is still NOT
    // safe (indices shift / the unique_ptr dies under us).
    const auto& entities = _app->GetEntities();
    for (size_t i = 0; i < entities.size(); ++i) {
        entities[i]->Tick(dt);
    }

    UIPageManager::Get()->Update(dt);
}

void GameLayer::OnRender(float dt) {
    // Note that draw calls are asynchronous, which means they return immediately.
    // So the drawing time can only be calculated along with the image presenting.
    // Generate UI commands
    RmlUiManager::Get()->Render();

    // Render the frame (executes all passes including UI)
    GraphicsSubsystem::Get()->Render(_app->GetMainCamera(), dt);
    // Nevertheless, glFinish() can force the GPU process all the commands synchronously.
    // glFinish();
}

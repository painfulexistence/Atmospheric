#include "Atmospheric.hpp"

// Editor shell — all scene management is driven by the JS bridge (wasm_api.cpp).
// OnLoad and OnUpdate are empty stubs; scenes arrive via ae_load_editor_scene_json.
class EditorShell final : public Application {
    using Application::Application;
    void OnLoad() override {}
    void OnUpdate(float, float) override {}
};

int main() {
    EditorShell app(AppConfig{
        .windowTitle = "Atmospheric Engine",
        .enableAudio = false,
        .preset = "2D",
    });
    app.Run();
    return 0;
}

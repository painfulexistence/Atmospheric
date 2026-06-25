// WASM editor bridge — Emscripten only.
//
// Exports C functions that the Project Cloudscape editor calls via ccall:
//   ae_load_editor_scene(ptr, len)         — CSB binary load (clears scene)
//   ae_load_editor_scene_json(jsonStr)     — JSON additive load (replace by name)
//   ae_add_scene(jsonStr)                  — JSON additive load (no replace)
//   ae_unload_scene(name)                  — destroy named scene container
//   ae_get_loaded_scenes()  → string       — JSON array of loaded scene names
//   ae_get_last_loaded_scene() → string    — name from most recent load
//   ae_get_scene_error()    → string       — error from most recent load
//   ae_reset_scene()                       — re-run OnLoad (full reload)
//   ae_get_version()        → string
//   ae_is_ready()           → int
//   ae_mount_file(path, ptr, len)

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include "application.hpp"
#include "file_system.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Pending scene buffer — written from JS, consumed on the next DeferSpawn tick.
static std::vector<uint8_t> g_pending_scene;

extern "C" {

// Load a CSB FlatBuffers binary (passed as a WASM heap pointer + length from JS).
// The actual scene swap is deferred to the start of the next game frame so it
// never interrupts an in-progress Update/Render cycle.
EMSCRIPTEN_KEEPALIVE
void ae_load_editor_scene(const uint8_t* data, size_t len)
{
    g_pending_scene.assign(data, data + len);

    auto* app = Application::Get();
    if (!app) return;

    app->DeferSpawn([buf = g_pending_scene]() {
        auto* a = Application::Get();
        if (a) a->LoadEditorScene(buf.data(), buf.size());
    });
}

// Additive scene load: replaces any existing scene with the same name under
// __root__, leaves all other loaded scenes intact. Deferred to next frame.
EMSCRIPTEN_KEEPALIVE
void ae_load_editor_scene_json(const char* json)
{
    if (!json) return;
    auto* app = Application::Get();
    if (!app) return;

    std::string jsonStr(json);
    app->DeferSpawn([app, jsonStr = std::move(jsonStr)]() {
        app->AddScene(jsonStr);
    });
}

// Reset the scene to the initial state (re-run OnLoad).
EMSCRIPTEN_KEEPALIVE
void ae_reset_scene()
{
    auto* app = Application::Get();
    if (!app) return;
    app->DeferSpawn([app]() { app->ReloadScene(); });
}

// Return the engine version string (matches CMakeLists VERSION).
EMSCRIPTEN_KEEPALIVE
const char* ae_get_version()
{
    return "0.2";
}

// Return 1 when the Application singleton is running, 0 otherwise.
// Useful for the JS bridge to poll readiness before sending scene data.
EMSCRIPTEN_KEEPALIVE
int ae_is_ready()
{
    return Application::Get() != nullptr ? 1 : 0;
}

// Mount a file (texture/audio/etc.) into the engine's virtual filesystem from a
// JS-supplied heap buffer. The editor must call this for every asset its scene
// references *before* ae_load_editor_scene, since there is no server to fetch
// them from. `path` is the virtual path the scene uses (e.g. "assets/hero.png").
// The bytes are copied immediately, so JS may free the heap pointer afterwards.
EMSCRIPTEN_KEEPALIVE
void ae_mount_file(const char* path, const uint8_t* data, size_t len)
{
    if (!path) return;
    FileSystem::Get().WriteFile(path, data, len);
}

// Return the last LoadEditorScene error, or "" if the most recent load
// succeeded (or no scene has been loaded yet). Lets the editor surface a
// structured failure reason instead of only seeing it in the console log.
EMSCRIPTEN_KEEPALIVE
const char* ae_get_scene_error()
{
    auto* app = Application::Get();
    if (!app) return "";
    return app->GetEditorSceneError().c_str();
}

// Additively load a JSON scene without replacing existing scenes of the same name.
// Useful for loading a second layer alongside an already-loaded scene.
EMSCRIPTEN_KEEPALIVE
void ae_add_scene(const char* json)
{
    if (!json) return;
    auto* app = Application::Get();
    if (!app) return;
    std::string jsonStr(json);
    app->DeferSpawn([app, jsonStr = std::move(jsonStr)]() {
        app->AddScene(jsonStr);
    });
}

// Destroy the named scene container and all its children.
EMSCRIPTEN_KEEPALIVE
void ae_unload_scene(const char* name)
{
    if (!name) return;
    auto* app = Application::Get();
    if (!app) return;
    std::string nameStr(name);
    app->DeferSpawn([app, nameStr = std::move(nameStr)]() {
        app->UnloadScene(nameStr);
    });
}

// Return a JSON array of currently loaded scene names, e.g. ["main_menu","hud"].
// ccall copies the returned pointer immediately, so the static buffer is safe.
EMSCRIPTEN_KEEPALIVE
const char* ae_get_loaded_scenes()
{
    auto* app = Application::Get();
    if (!app) return "[]";
    static std::string buf;
    buf = app->GetLoadedScenes();
    return buf.c_str();
}

// Return the name of the scene most recently loaded by AddScene /
// LoadEditorSceneFromJson, or "" if the last load failed.
EMSCRIPTEN_KEEPALIVE
const char* ae_get_last_loaded_scene()
{
    auto* app = Application::Get();
    if (!app) return "";
    static std::string buf;
    buf = app->GetLastLoadedScene();
    return buf.c_str();
}

} // extern "C"

#endif // __EMSCRIPTEN__

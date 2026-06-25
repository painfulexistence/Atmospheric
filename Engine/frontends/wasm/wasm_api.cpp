// WASM editor bridge — Emscripten only.
//
// Exports C functions that the Project Cloudscape editor calls via:
//   Module.ccall('ae_load_editor_scene', null, ['number','number'], [ptr, len])
//   Module.ccall('ae_reset_scene',        null, [], [])
//   Module.ccall('ae_get_version',         'string', [], [])
//   Module.ccall('ae_is_ready',            'number', [], [])
//   Module.ccall('ae_mount_file',          null, ['string','number','number'], [path, ptr, len])
//   Module.ccall('ae_get_scene_error',     'string', [], [])
//
// Required CMakeLists link flags (already added):
//   -sEXPORTED_FUNCTIONS=['_main','_malloc','_free',
//                          '_ae_load_editor_scene','_ae_reset_scene',
//                          '_ae_get_version','_ae_is_ready',
//                          '_ae_mount_file','_ae_get_scene_error']
//   -sEXPORTED_RUNTIME_METHODS=["requestFullscreen","ccall","HEAPU8"]

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
    // Returned pointer stays valid until the next LoadEditorScene call; ccall
    // copies it into a JS string immediately, so this is safe.
    return app->GetEditorSceneError().c_str();
}

} // extern "C"

#endif // __EMSCRIPTEN__

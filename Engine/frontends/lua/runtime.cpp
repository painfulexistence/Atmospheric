/// AtmosLua - Love2D-style Lua Game Runtime
///
/// This is the entry point for running Lua games with the Atmospheric Engine.
/// It creates a LuaApplication that loads and executes main.lua.
///
/// Usage:
///   ./AtmosLua [options]
///
/// The runtime looks for main.lua in the following locations (in order):
///   1. ./assets/scripts/main.lua
///   2. ./assets/main.lua
///   3. ./main.lua
///
/// Example main.lua:
///   function load()
///       player = atmos.world.spawn(0, 1, 0)
///       player.name = "Player"
///   end
///
///   function update(dt)
///       if atmos.input.isKeyDown(atmos.keys.ESCAPE) then
///           atmos.quit()
///       end
///   end

#include "lua_application.hpp"
#include "Atmospheric/file_system.hpp"

#ifdef __EMSCRIPTEN__
// ─────────────────────────────────────────────────────────────────────────────
// Web / WebAssembly entry point
//
// FileSystem::Prefetch handles everything in a single call:
//
//   KTX2 textures   → in-process cache, consumed by LoadKTX2Texture()
//   Lua scripts     → in-process cache + Emscripten MEMFS
//                     (FileSystem auto-detects .lua extension)
//                     makes files visible to fopen() and Lua's io/require()
//
// All files are also persisted in IndexedDB so subsequent page-loads skip
// the network entirely.
//
// Asset manifests
// ───────────────
// IMPORTANT: every .lua file that can be require()'d at runtime must appear
// in kAssets.  Lua's require() searches package.path (fopen) at runtime, so
// any missing file will cause a "module not found" error mid-game.
//
// Texture files must be KTX2 format; convert with:
//   basisu -ktx2 -mipmap source.jpg
//   toktx --t2 --encode etc1s --mipmap out.ktx2 source.jpg
// ─────────────────────────────────────────────────────────────────────────────

static const std::vector<std::string> kAssets = {
    // KTX2 textures (binary → cache only, consumed by LoadKTX2Texture)
    "assets/textures/default_diff.ktx2",
    "assets/textures/default_norm.ktx2",
    "assets/textures/default_ao.ktx2",
    "assets/textures/default_rough.ktx2",
    "assets/textures/default_metallic.ktx2",

    // Lua scripts (text → cache + MEMFS, so fopen/require work)
    // Add every .lua file your game uses, including require()'d modules:
    "assets/scripts/main.lua",
    // "assets/scripts/components/player.lua",
    // "assets/scripts/utils/helpers.lua",
};

static void StartGame();

int main(int argc, char* argv[]) {
    // Prefetch all assets in one call.
    //   • KTX2 files are stored in the FileSystem cache.
    //   • .lua files are stored in the cache AND written to MEMFS.
    //   • All files are persisted to IndexedDB for offline / fast reload.
    // main() returns immediately; Emscripten's event loop keeps the page alive.
    // StartGame() fires asynchronously once all fetches settle.
    FileSystem::Get().Prefetch(kAssets, StartGame);
    return 0;
}

static void StartGame() {
    // At this point:
    //   FileSystem cache has all KTX2 bytes  → LoadDefaultTextures() uses them
    //   MEMFS has all .lua files              → LoadUserScripts() can fopen them
    static LuaApplication app({
        .windowTitle        = "AtmosLua",
        .windowWidth        = 1280,
        .windowHeight       = 720,
        .windowResizable    = true,
        .vsync              = true,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    app.Run(); // installs emscripten_set_main_loop; never returns
}

#else
// ─────────────────────────────────────────────────────────────────────────────
// Native entry point (Linux / macOS / Windows)
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    LuaApplication app({
        .windowTitle        = "AtmosLua",
        .windowWidth        = 1280,
        .windowHeight       = 720,
        .windowResizable    = true,
        .vsync              = true,
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    app.Run();
    return 0;
}
#endif

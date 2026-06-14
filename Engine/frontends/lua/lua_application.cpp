#include "lua_application.hpp"
#include "Atmospheric/gfx_factory.hpp"

// std::filesystem maps to Emscripten's MEMFS on web builds;
// text files written there by FileSystem::Prefetch() are visible here.
#include <filesystem>

// Forward declarations for binding functions
void BindCoreTypes(sol::state& lua);
void BindInputAPI(sol::state& lua, Input* input);
void BindWorldAPI(sol::state& lua, LuaApplication* app);
void BindGraphicsAPI(sol::state& lua, GraphicsServer* graphics);
void BindPhysicsAPI(sol::state& lua, LuaApplication* app);
void BindAudioAPI(sol::state& lua, AudioManager* audio);

LuaApplication::LuaApplication(AppConfig config) : Application(config) {
}

LuaApplication::~LuaApplication() {
}

void LuaApplication::OnInit() {
    GoScene("main", [this]{ OnLoad(); });

    // Load default shaders and textures (skip in WebGPU mode — no GL context)
#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    if (GfxFactory::GetBackend() != GfxBackend::WebGPU)
#endif
    AssetManager::Get().LoadDefaultShaders();
    AssetManager::Get().LoadDefaultTextures();

    // Create a default material
    MaterialProps defaultProps;
    defaultProps.diffuse = glm::vec3(1.0f);
    AssetManager::Get().CreateMaterial("Default", defaultProps);

    InitializeLua();
    BindEngineAPIs();
    LoadUserScripts();
    CacheCallbacks();
}

void LuaApplication::OnLoad() {
    // Call parent to set up basic scene infrastructure
    // (User can override by not calling atmos.scene.loadDefault())

    // Call Lua load() callback
    if (_luaLoad.valid()) {
        CallLua(_luaLoad, "load");
    }
}

void LuaApplication::OnUpdate(float dt, float time) {
    // Call Lua update(dt) callback
    if (_luaUpdate.valid()) {
        CallLua(_luaUpdate, "update", dt);
    }

    // Handle keyboard events
    // TODO: Implement proper event system instead of polling
    // For now, we'll let Lua poll input via atmos.input.isKeyDown()
}

void LuaApplication::InitializeLua() {
    // Open standard Lua libraries.
    // On Emscripten sol::lib::io and sol::lib::os map to MEMFS; text files
    // written there by FileSystem::Prefetch() are accessible via io.open()
    // and the Lua require() system.
    _lua.open_libraries(
      sol::lib::base,
      sol::lib::package,
      sol::lib::string,
      sol::lib::math,
      sol::lib::table,
      sol::lib::io,
      sol::lib::os,
      sol::lib::debug
    );

    // Set up package path for require().
    // This makes require("components.player") find
    // ./assets/scripts/components/player.lua in MEMFS (web) or on disk (native).
    std::string packagePath = _lua["package"]["path"];
    packagePath += ";./assets/scripts/?.lua";
    packagePath += ";./assets/scripts/?/init.lua";
    _lua["package"]["path"] = packagePath;

    ENGINE_LOG("Lua environment initialized");
}

void LuaApplication::BindEngineAPIs() {
    // Create atmos namespace
    sol::table atmos = _lua.create_named_table("atmos");

    // Bind core types (vec2, vec3, etc.)
    BindCoreTypes(_lua);

    // Bind Input API
    BindInputAPI(_lua, GetInput());

    // Bind World/Scene API
    BindWorldAPI(_lua, this);

    // Bind Graphics API
    BindGraphicsAPI(_lua, GetGraphicsServer());

    // Bind Physics API
    BindPhysicsAPI(_lua, this);

    // Bind Audio API
    BindAudioAPI(_lua, GetAudioManager());

    // Bind application-level functions
    atmos["quit"] = [this]() { Quit(); };
    atmos["getTime"] = [this]() { return GetWindowTime(); };
    atmos["getDeltaTime"] = []() {
        // TODO: Store dt in a accessible place
        return 0.016f;
    };

    ENGINE_LOG("Engine APIs bound to Lua");
}

void LuaApplication::LoadUserScripts() {
    // Look for main.lua in assets/scripts/
    std::vector<std::string> scriptPaths = { "./assets/scripts/main.lua", "./assets/main.lua", "./main.lua" };

    bool loaded = false;
    for (const auto& path : scriptPaths) {
        if (std::filesystem::exists(path)) {
            auto result = _lua.safe_script_file(path, sol::script_pass_on_error);
            if (!result.valid()) {
                HandleError(result, "Loading " + path);
            } else {
                ENGINE_LOG("Loaded script: {}", path);
                loaded = true;
                break;
            }
        }
    }

    if (!loaded) {
        ENGINE_LOG("Warning: No main.lua found. Create one in assets/scripts/main.lua");
    }
}

void LuaApplication::CacheCallbacks() {
    // Cache global callbacks for efficient access
    auto tryCache = [this](const char* name) -> sol::protected_function {
        sol::object obj = _lua[name];
        if (obj.is<sol::protected_function>()) {
            return obj.as<sol::protected_function>();
        }
        return sol::lua_nil;
    };

    _luaLoad = tryCache("load");
    _luaUpdate = tryCache("update");
    _luaDraw = tryCache("draw");
    _luaKeypressed = tryCache("keypressed");
    _luaKeyreleased = tryCache("keyreleased");

    if (_luaLoad.valid()) ENGINE_LOG("Found load() callback");
    if (_luaUpdate.valid()) ENGINE_LOG("Found update() callback");
    if (_luaDraw.valid()) ENGINE_LOG("Found draw() callback");
}

void LuaApplication::HandleError(const sol::protected_function_result& result, const std::string& context) {
    sol::error err = result;
    std::string errorMsg = err.what();

    // Print error with context
    fmt::print(stderr, "[Lua Error] {}: {}\n", context, errorMsg);

    // TODO: Show error overlay like Love2D's blue screen
    // For now, just log it
    ENGINE_LOG("Lua error in {}: {}", context, errorMsg);
}

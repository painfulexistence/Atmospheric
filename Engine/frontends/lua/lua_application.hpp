#pragma once

#include "Atmospheric.hpp"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

/// LuaApplication - A Love2D-style Lua game runtime
///
/// This class provides a complete Lua scripting environment where Lua code
/// controls the game logic through callbacks (load, update, draw).
///
/// Usage:
///   1. Build AtmosLua executable
///   2. Place main.lua in assets/scripts/
///   3. Run AtmosLua
///
/// Lua callbacks:
///   - load()      : Called once after engine initialization
///   - update(dt)  : Called every frame with delta time
///   - draw()      : Called every frame for rendering (optional)
///   - keypressed(key)   : Called on key press
///   - keyreleased(key)  : Called on key release
///
class LuaApplication : public Application {
public:
    explicit LuaApplication(AppConfig config = {});
    ~LuaApplication() override;

    /// Access the Lua state (for advanced usage)
    sol::state& GetLuaState() {
        return _lua;
    }

protected:
    void OnInit() override;
    void OnLoad() override;
    void OnUpdate(float dt, float time) override;

private:
    sol::state _lua;

    // Cached Lua callbacks for performance
    sol::protected_function _luaLoad;
    sol::protected_function _luaUpdate;
    sol::protected_function _luaDraw;
    sol::protected_function _luaKeypressed;
    sol::protected_function _luaKeyreleased;

    /// Initialize Lua environment and open standard libraries
    void InitializeLua();

    /// Bind all engine APIs to Lua (atmos.* namespace)
    void BindEngineAPIs();

    /// Load and execute user scripts
    void LoadUserScripts();

    /// Cache Lua callback functions for efficient calling
    void CacheCallbacks();

    /// Handle Lua errors with proper error messages
    void HandleError(const sol::protected_function_result& result, const std::string& context);

    /// Call a Lua function safely with error handling
    template<typename... Args> void CallLua(sol::protected_function& func, const std::string& name, Args&&... args) {
        auto result = func(std::forward<Args>(args)...);
        if (!result.valid()) {
            HandleError(result, name + "()");
        }
    }
};

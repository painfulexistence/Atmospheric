#pragma once

#include "Atmospheric.hpp"
#include <sol/sol.hpp>
#include <string>

class LuaApplication;

/// ScriptableComponent - A C++ component container for Lua-defined behaviors
///
/// This allows Lua scripts to define custom component logic that integrates
/// with the C++ component lifecycle (OnAttach, OnTick, etc.)
///
/// Usage in Lua:
///   -- Define a script class
///   PlayerController = {}
///   PlayerController.__index = PlayerController
///
///   function PlayerController:new()
///     local self = setmetatable({}, PlayerController)
///     self.speed = 5.0
///     return self
///   end
///
///   function PlayerController:init()
///     print("PlayerController initialized")
///   end
///
///   function PlayerController:update(dt)
///     -- movement logic
///   end
///
///   -- Attach to a GameObject
///   local player = atmos.world.spawn()
///   player:addScript("PlayerController")
///
class ScriptableComponent : public Component {
public:
    /// Construct a ScriptableComponent with a Lua class name
    /// @param gameObject The owning GameObject
    /// @param lua Reference to the Lua state
    /// @param className Name of the Lua class to instantiate
    ScriptableComponent(GameObject* gameObject, sol::state& lua, const std::string& className);

    ~ScriptableComponent() override;

    std::string GetName() const override;

    /// Called when attached to a GameObject
    void OnAttach() override;

    /// Called when detached from a GameObject
    void OnDetach() override;

    /// Called every frame
    void OnTick(float dt) override;

    /// Override CanTick to optimize updates
    bool CanTick() const override {
        return enabled && _updateFunc.valid();
    }

    /// Called on physics tick (if the script defines it)
    void OnPhysicsTick(float dt) override;

    /// Override CanPhysicsTick to optimize updates
    bool CanPhysicsTick() const override {
        return enabled && _physicsUpdateFunc.valid();
    }

    /// Handle collision events (called from C++ collision system)
    void OnCollision(GameObject* other);

    /// Get the Lua instance table for advanced manipulation
    sol::table& GetInstance() {
        return _instance;
    }

    /// Get the class name
    const std::string& GetClassName() const {
        return _className;
    }

    /// Call a custom method on the Lua instance
    template<typename... Args> void CallMethod(const std::string& methodName, Args&&... args);

    /// Check if the script has a specific method
    bool HasMethod(const std::string& methodName) const;

private:
    sol::state& _lua;
    std::string _className;
    sol::table _instance;

    // Cached method references for performance
    sol::protected_function _initFunc;
    sol::protected_function _updateFunc;
    sol::protected_function _physicsUpdateFunc;
    sol::protected_function _onCollisionFunc;
    sol::protected_function _onDestroyFunc;

    /// Create an instance of the Lua class
    bool CreateInstance();

    /// Cache commonly used methods
    void CacheMethods();

    /// Safe method call with error handling
    void CallMethodSafe(sol::protected_function& func, const std::string& methodName);

    template<typename... Args>
    void CallMethodSafeWithArgs(sol::protected_function& func, const std::string& methodName, Args&&... args);
};

// Template implementation
template<typename... Args> void ScriptableComponent::CallMethod(const std::string& methodName, Args&&... args) {
    if (!_instance.valid()) return;

    sol::object methodObj = _instance[methodName];
    if (methodObj.is<sol::protected_function>()) {
        sol::protected_function method = methodObj.as<sol::protected_function>();
        auto result = method(_instance, std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error err = result;
            fmt::print(stderr, "[Lua Error] {}:{} - {}\n", _className, methodName, err.what());
        }
    }
}

template<typename... Args>
void ScriptableComponent::CallMethodSafeWithArgs(
  sol::protected_function& func, const std::string& methodName, Args&&... args
) {
    if (!func.valid()) return;

    auto result = func(_instance, std::forward<Args>(args)...);
    if (!result.valid()) {
        sol::error err = result;
        fmt::print(stderr, "[Lua Error] {}:{} - {}\n", _className, methodName, err.what());
    }
}

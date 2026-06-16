#include "scriptable_component.hpp"

ScriptableComponent::ScriptableComponent(GameObject* go, sol::state& lua, const std::string& className)
  : _lua(lua), _className(className) {
    gameObject = go;

    if (!CreateInstance()) {
        fmt::print(stderr, "[ScriptableComponent] Failed to create instance of class '{}'\n", _className);
    }
}

ScriptableComponent::~ScriptableComponent() {
    // Call onDestroy if defined
    if (_onDestroyFunc.valid()) {
        CallMethodSafe(_onDestroyFunc, "onDestroy");
    }

    // Clear references to allow Lua GC
    _instance = sol::lua_nil;
    _initFunc = sol::lua_nil;
    _updateFunc = sol::lua_nil;
    _physicsUpdateFunc = sol::lua_nil;
    _onCollisionFunc = sol::lua_nil;
    _onDestroyFunc = sol::lua_nil;
}

std::string ScriptableComponent::GetName() const {
    return "ScriptableComponent:" + _className;
}

bool ScriptableComponent::CreateInstance() {
    // Look up the class in global scope
    sol::object classObj = _lua[_className];
    if (!classObj.valid()) {
        fmt::print(stderr, "[ScriptableComponent] Class '{}' not found in Lua state\n", _className);
        return false;
    }

    // Check if it's a table (Lua class)
    if (!classObj.is<sol::table>()) {
        fmt::print(stderr, "[ScriptableComponent] '{}' is not a table/class\n", _className);
        return false;
    }

    sol::table classTable = classObj.as<sol::table>();

    // Try to call the 'new' constructor if it exists
    sol::object newFunc = classTable["new"];
    if (newFunc.is<sol::protected_function>()) {
        sol::protected_function constructor = newFunc.as<sol::protected_function>();
        auto result = constructor(classTable);

        if (!result.valid()) {
            sol::error err = result;
            fmt::print(stderr, "[ScriptableComponent] Error calling {}.new(): {}\n", _className, err.what());
            return false;
        }

        sol::object instance = result;
        if (instance.is<sol::table>()) {
            _instance = instance.as<sol::table>();
        } else {
            fmt::print(stderr, "[ScriptableComponent] {}.new() did not return a table\n", _className);
            return false;
        }
    } else {
        // No 'new' constructor - create a simple table that inherits from the class
        _instance = _lua.create_table();

        // Set metatable to inherit from the class
        sol::table metatable = _lua.create_table();
        metatable["__index"] = classTable;
        _instance[sol::metatable_key] = metatable;
    }

    // Store reference to the GameObject in the Lua instance
    _instance["gameObject"] = gameObject;

    // Cache methods for performance
    CacheMethods();

    return true;
}

void ScriptableComponent::CacheMethods() {
    if (!_instance.valid()) return;

    // Helper to cache a method
    auto cacheMethod = [this](const std::string& name) -> sol::protected_function {
        sol::object obj = _instance[name];
        if (obj.is<sol::protected_function>()) {
            return obj.as<sol::protected_function>();
        }
        return sol::lua_nil;
    };

    _initFunc = cacheMethod("init");
    _updateFunc = cacheMethod("update");
    _physicsUpdateFunc = cacheMethod("physicsUpdate");
    _onCollisionFunc = cacheMethod("onCollision");
    _onDestroyFunc = cacheMethod("onDestroy");
}

void ScriptableComponent::OnAttach() {
    if (_initFunc.valid()) {
        CallMethodSafe(_initFunc, "init");
    }
}

void ScriptableComponent::OnDetach() {
    // onDestroy is called in destructor instead
}

void ScriptableComponent::OnTick(float dt) {
    if (_updateFunc.valid()) {
        CallMethodSafeWithArgs(_updateFunc, "update", dt);
    }
}

void ScriptableComponent::OnPhysicsTick(float dt) {
    if (_physicsUpdateFunc.valid()) {
        CallMethodSafeWithArgs(_physicsUpdateFunc, "physicsUpdate", dt);
    }
}

void ScriptableComponent::OnCollision(GameObject* other) {
    if (_onCollisionFunc.valid()) {
        CallMethodSafeWithArgs(_onCollisionFunc, "onCollision", other);
    }
}

bool ScriptableComponent::HasMethod(const std::string& methodName) const {
    if (!_instance.valid()) return false;

    sol::object obj = _instance[methodName];
    return obj.is<sol::protected_function>();
}

void ScriptableComponent::CallMethodSafe(sol::protected_function& func, const std::string& methodName) {
    if (!func.valid()) return;

    auto result = func(_instance);
    if (!result.valid()) {
        sol::error err = result;
        fmt::print(stderr, "[Lua Error] {}:{} - {}\n", _className, methodName, err.what());
    }
}

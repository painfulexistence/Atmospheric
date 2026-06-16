#pragma once
#include <functional>
#include <string>
#include <unordered_map>

class Component;
class GameObject;

class ComponentRegistry {
public:
    // creator receives (owner, props) where props may be nullptr to use defaults
    using CreatorFunc = std::function<Component*(GameObject*, const void*)>;

    static void Register(const std::string& typeName, CreatorFunc creator) {
        GetRegistry()[typeName] = std::move(creator);
    }

    static Component* Create(const std::string& typeName, GameObject* owner, const void* props = nullptr) {
        auto& reg = GetRegistry();
        auto it = reg.find(typeName);
        if (it != reg.end()) return it->second(owner, props);
        return nullptr;
    }

    static bool Has(const std::string& typeName) {
        return GetRegistry().count(typeName) > 0;
    }

private:
    static std::unordered_map<std::string, CreatorFunc>& GetRegistry();
};

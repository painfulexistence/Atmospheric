#include "component_factory.hpp"
#include "game_object.hpp"
#include <spdlog/spdlog.h>

std::unordered_map<std::string, ComponentFactory::CreatorFunc>& ComponentFactory::GetRegistry() {
    static std::unordered_map<std::string, ComponentFactory::CreatorFunc> instance;
    return instance;
}

Component* ComponentFactory::Create(const std::string& typeName, GameObject* owner, Deserializer& d) {
    auto& reg = GetRegistry();
    auto it = reg.find(typeName);
    if (it != reg.end()) {
        Component* comp = it->second(owner, d);
        if (comp) owner->AddComponent(comp);
        return comp;
    }
    spdlog::warn("[ComponentFactory] Unknown component type: '{}' — skipping", typeName);
    return nullptr;
}

void ComponentFactory::Alias(const std::string& aliasName, const std::string& targetName) {
    auto& reg = GetRegistry();
    auto it = reg.find(targetName);
    if (it == reg.end()) {
        spdlog::warn("[ComponentFactory] Alias target '{}' not registered — alias '{}' skipped", targetName,
                     aliasName);
        return;
    }
    reg[aliasName] = it->second;
}

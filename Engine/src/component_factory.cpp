#include "component_factory.hpp"
#include <spdlog/spdlog.h>

std::unordered_map<std::string, ComponentFactory::CreatorFunc>& ComponentFactory::GetRegistry() {
    static std::unordered_map<std::string, ComponentFactory::CreatorFunc> instance;
    return instance;
}

Component* ComponentFactory::Create(const std::string& typeName, GameObject* owner, Deserializer& d) {
    auto& reg = GetRegistry();
    auto it = reg.find(typeName);
    if (it != reg.end()) return it->second(owner, d);
    spdlog::warn("[ComponentFactory] Unknown component type: '{}' — skipping", typeName);
    return nullptr;
}

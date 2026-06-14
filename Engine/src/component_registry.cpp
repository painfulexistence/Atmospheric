#include "component_registry.hpp"

std::unordered_map<std::string, ComponentRegistry::CreatorFunc>& ComponentRegistry::GetRegistry() {
    static std::unordered_map<std::string, ComponentRegistry::CreatorFunc> instance;
    return instance;
}

#pragma once
#include "deserializer.hpp"
#include <functional>
#include <string>
#include <unordered_map>

class Component;
class GameObject;

// ── ComponentFactory ──────────────────────────────────────────────────────────
//
// Dynamic factory for Components.  Each component type registers a creator
// lambda that receives a Deserializer& so it can pull its fields in a
// format-agnostic way (JSON today, Binary / USD tomorrow).
//
// Registration (typically in Application::RegisterComponents):
//
//   ComponentFactory::Register("SpriteComponent",
//     [](GameObject* o, Deserializer& d) -> Component* {
//         glm::vec2 size(100.f);
//         d.Read("size", size);
//         auto* s = new SpriteComponent(o, SpriteProps{});
//         s->SetSize(size);
//         return s;
//     });
//
// Creators must return a NEW, not-yet-attached component: Create() attaches
// it exactly once and the GameObject takes ownership there.  Never call
// owner->AddComponent or ComponentFactory::Create inside a creator — that
// would attach (and own) the component twice.
//
// Instantiation from a scene loader:
//
//   JSONDeserializer compD(compJson);
//   ComponentFactory::Create(type, gameObject, compD);  // no if-else needed

class ComponentFactory {
public:
    using CreatorFunc = std::function<Component*(GameObject*, Deserializer&)>;

    static void Register(const std::string& typeName, CreatorFunc creator) {
        GetRegistry()[typeName] = std::move(creator);
    }

    // Create a component of typeName on owner, populating its fields via d.
    // Returns nullptr (and logs a warning) if the type is not registered.
    static Component* Create(const std::string& typeName, GameObject* owner, Deserializer& d);

    static bool Has(const std::string& typeName) {
        return GetRegistry().contains(typeName);
    }

private:
    static std::unordered_map<std::string, CreatorFunc>& GetRegistry();
};

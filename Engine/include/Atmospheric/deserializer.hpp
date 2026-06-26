#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// ── Deserializer ──────────────────────────────────────────────────────────────
//
// Abstract pull-based field reader used by ComponentFactory to initialize
// components without coupling to a specific serialization format.
//
// Design goals:
//   • Lenient / default-value-first — missing keys never throw; they silently
//     fall back to the caller-supplied default.  This is critical for WASM
//     builds compiled with -fno-exceptions and for forward-compatibility when
//     new fields are added to a component but old scene files don't have them
//     yet.
//   • GLM-native — vec2/vec3/vec4 are first-class so component registration
//     lambdas don't need per-project conversion helpers.
//   • Hierarchical — ReadObject / ReadArray let the same interface walk a full
//     scene node tree without knowing whether the backing storage is JSON,
//     FlatBuffers, a USD Prim, or anything else.
//
// Usage in a ComponentFactory lambda:
//
//   ComponentFactory::Register("SpriteComponent",
//     [](GameObject* o, Deserializer& d) -> Component* {
//         glm::vec2 size(100.f);
//         glm::vec4 color(1.f);
//         d.Read("size",  size);
//         d.Read("color", color);
//         auto* s = o->AddComponent<SpriteComponent>(SpriteProps{});
//         s->SetSize(size);
//         s->SetColor(color);
//         return s;
//     });
//
// Concrete implementations:
//   JSONDeserializer  — backed by nlohmann::json  (json_deserializer.hpp)
//
// Future implementations (zero component-code changes required):
//   BinaryDeserializer — backed by a hand-written or FlatBuffers byte stream
//   USDPrimDeserializer — backed by a pxr::UsdPrim attribute read

class Deserializer {
public:
    virtual ~Deserializer() = default;

    // ── Scalar reads ─────────────────────────────────────────────────────────
    virtual void Read(const char* name, float&       out, float       defaultVal = 0.0f)  = 0;
    virtual void Read(const char* name, int&         out, int         defaultVal = 0)     = 0;
    virtual void Read(const char* name, bool&        out, bool        defaultVal = false) = 0;
    virtual void Read(const char* name, std::string& out, const std::string& defaultVal = "") = 0;
    virtual void Read(const char* name, std::vector<int>& out) = 0;

    // ── GLM reads ────────────────────────────────────────────────────────────
    virtual void Read(const char* name, glm::vec2& out,
                      const glm::vec2& defaultVal = glm::vec2(0.f)) = 0;
    virtual void Read(const char* name, glm::vec3& out,
                      const glm::vec3& defaultVal = glm::vec3(0.f)) = 0;
    virtual void Read(const char* name, glm::vec4& out,
                      const glm::vec4& defaultVal = glm::vec4(0.f)) = 0;

    // ── Structural reads ─────────────────────────────────────────────────────
    // Returns a Deserializer focused on the named sub-object.
    // Returns nullptr if the key doesn't exist or is not an object.
    virtual std::unique_ptr<Deserializer> ReadObject(const char* name) = 0;

    // Returns one Deserializer per element of the named array.
    // Returns an empty vector if the key doesn't exist or is not an array.
    virtual std::vector<std::unique_ptr<Deserializer>> ReadArray(const char* name) = 0;

    // Convenience: check whether a key exists before reading it.
    virtual bool Has(const char* name) const = 0;
};

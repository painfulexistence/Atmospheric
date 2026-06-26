#pragma once
#include "deserializer.hpp"
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>

// ── JSONDeserializer ──────────────────────────────────────────────────────────
//
// Concrete Deserializer backed by a nlohmann::json value.
//
// Key design decisions:
//
//   1. Lenient — every Read() accepts a default value and falls back to it
//      silently when the key is missing or has a mismatched type.  No
//      exceptions are thrown, so this works with -fno-exceptions (WASM).
//
//   2. GLM arrays — vec2/vec3/vec4 are read from JSON arrays.  If the array
//      is shorter than expected, the remaining components take the default.
//
//   3. ReadArray stores the *value* (not a reference) of each element so the
//      returned Deserializer children remain valid after the parent JSON object
//      goes out of scope in a range-for.
//
// Usage:
//   nlohmann::json j = ...;
//   JSONDeserializer d(j);
//   ComponentFactory::Create("SpriteComponent", gameObject, d);

class JSONDeserializer : public Deserializer {
public:
    explicit JSONDeserializer(const nlohmann::json& j) : _j(j) {}

    // ── Scalar reads ─────────────────────────────────────────────────────────

    void Read(const char* name, float& out, float defaultVal) override {
        out = _j.value(name, defaultVal);
    }

    void Read(const char* name, int& out, int defaultVal) override {
        out = _j.value(name, defaultVal);
    }

    void Read(const char* name, bool& out, bool defaultVal) override {
        out = _j.value(name, defaultVal);
    }

    void Read(const char* name, std::string& out, const std::string& defaultVal) override {
        out = _j.value(name, defaultVal);
    }

    void Read(const char* name, std::vector<int>& out) override {
        out.clear();
        if (_j.contains(name) && _j[name].is_array()) {
            for (const auto& elem : _j[name]) {
                if (elem.is_number_integer()) {
                    out.push_back(elem.get<int>());
                }
            }
        }
    }

    // ── GLM reads ─────────────────────────────────────────────────────────────

    void Read(const char* name, glm::vec2& out, const glm::vec2& defaultVal) override {
        out = defaultVal;
        if (_j.contains(name) && _j[name].is_array()) {
            const auto& arr = _j[name];
            if (arr.size() >= 1) out.x = arr[0].get<float>();
            if (arr.size() >= 2) out.y = arr[1].get<float>();
        }
    }

    void Read(const char* name, glm::vec3& out, const glm::vec3& defaultVal) override {
        out = defaultVal;
        if (_j.contains(name) && _j[name].is_array()) {
            const auto& arr = _j[name];
            if (arr.size() >= 1) out.x = arr[0].get<float>();
            if (arr.size() >= 2) out.y = arr[1].get<float>();
            if (arr.size() >= 3) out.z = arr[2].get<float>();
        }
    }

    void Read(const char* name, glm::vec4& out, const glm::vec4& defaultVal) override {
        out = defaultVal;
        if (_j.contains(name) && _j[name].is_array()) {
            const auto& arr = _j[name];
            if (arr.size() >= 1) out.x = arr[0].get<float>();
            if (arr.size() >= 2) out.y = arr[1].get<float>();
            if (arr.size() >= 3) out.z = arr[2].get<float>();
            if (arr.size() >= 4) out.w = arr[3].get<float>();
        }
    }

    // ── Structural reads ──────────────────────────────────────────────────────

    std::unique_ptr<Deserializer> ReadObject(const char* name) override {
        if (_j.contains(name) && _j[name].is_object())
            return std::make_unique<JSONDeserializer>(_j[name]);
        return nullptr;
    }

    std::vector<std::unique_ptr<Deserializer>> ReadArray(const char* name) override {
        std::vector<std::unique_ptr<Deserializer>> result;
        if (_j.contains(name) && _j[name].is_array()) {
            for (const auto& elem : _j[name])
                result.push_back(std::make_unique<JSONDeserializer>(elem));
        }
        return result;
    }

    bool Has(const char* name) const override {
        return _j.contains(name);
    }

private:
    // Stored by value so children created by ReadArray keep their own copy.
    nlohmann::json _j;
};

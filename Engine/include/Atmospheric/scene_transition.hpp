#pragma once
#include <functional>
#include <string>
#include <vector>

#include "shader.hpp"
#include <unordered_map>

// Describes the assets required by one game/scene.
// Loaded from assets/scenes/<name>.json.
struct GameManifest {
    std::string              name;
    std::vector<std::string> textures;
    std::unordered_map<std::string, ShaderProgramProps> shaders;

    static GameManifest FromJSON(const std::string& json);
};

// Async scene transition: prefetch assets → clear previous scene → load new assets → callback.
//
// Manifest path convention: assets/scenes/<sceneName>.json
//
// Usage:
//   SceneTransition::Go("poker", [this]{
//       // assets ready — instantiate game objects, hide loading screen
//   });
//
// Loading UI is the caller's responsibility.
class SceneTransition {
public:
    using OnReadyFn = std::function<void()>;
    using OnErrorFn = std::function<void(const std::string& reason)>;

    static void Go(const std::string& sceneName, OnReadyFn onReady, OnErrorFn onError = nullptr,
                   const std::string& currentSceneName = "");

private:
    static constexpr const char* kManifestDir = "assets/scenes/";
};

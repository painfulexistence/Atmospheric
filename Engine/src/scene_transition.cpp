#include "scene_transition.hpp"
#include "asset_manager.hpp"
#include "file_system.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// ── GameManifest ─────────────────────────────────────────────────────────────

static GameManifest ParseJSON(const std::string& json)
{
    GameManifest manifest;
    try {
        auto j = nlohmann::json::parse(json);
        manifest.name = j.value("name", "");

        if (j.contains("textures"))
            for (auto& v : j["textures"])
                manifest.textures.push_back(v.get<std::string>());

        if (j.contains("shaders")) {
            if (j["shaders"].is_object()) {
                for (auto& [name, shaderVal] : j["shaders"].items()) {
                    ShaderProgramProps props;
                    props.vert = shaderVal.value("vert", "");
                    props.frag = shaderVal.value("frag", "");
                    if (shaderVal.contains("tesc")) {
                        props.tesc = shaderVal["tesc"].get<std::string>();
                    }
                    if (shaderVal.contains("tese")) {
                        props.tese = shaderVal["tese"].get<std::string>();
                    }
                    manifest.shaders[name] = props;
                }
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("GameManifest: JSON parse error: {}", e.what());
    }
    return manifest;
}

GameManifest GameManifest::FromJSON(const std::string& json)
{
    return ParseJSON(json);
}

// ── ClearScene ───────────────────────────────────────────────────────────────
// Clears all scene-specific GPU resources, preserving default textures.
// TODO: preserve default shaders once AssetManager tracks them separately.

static void ClearScene()
{
    AssetManager::Get().ClearSceneAssets();
}

// ── SceneTransition::Go ──────────────────────────────────────────────────────

void SceneTransition::Go(const std::string& sceneName, OnReadyFn onReady, OnErrorFn onError,
                         const std::string& currentSceneName)
{
    const std::string manifestPath = std::string(kManifestDir) + sceneName + ".json";

    bool shouldClear = !currentSceneName.empty();
    FileSystem::Get().Prefetch({ manifestPath }, [sceneName, manifestPath, shouldClear, onReady, onError]() {
        auto bytes = FileSystem::Get().ReadSync(manifestPath);
        if (bytes.empty()) {
            std::string reason = "failed to read manifest: " + manifestPath;
            spdlog::error("SceneTransition: {}", reason);
            if (onError) onError(reason);
            return;
        }

        auto manifest = GameManifest::FromJSON(std::string(bytes.begin(), bytes.end()));

        std::vector<std::string> allPaths;
        allPaths.insert(allPaths.end(), manifest.textures.begin(), manifest.textures.end());
        for (const auto& [name, props] : manifest.shaders) {
            if (!props.vert.empty()) allPaths.push_back(props.vert);
            if (!props.frag.empty()) allPaths.push_back(props.frag);
            if (props.tesc.has_value() && !props.tesc.value().empty()) allPaths.push_back(props.tesc.value());
            if (props.tese.has_value() && !props.tese.value().empty()) allPaths.push_back(props.tese.value());
        }

        spdlog::info("SceneTransition: prefetching {} asset(s) for '{}'",
                     allPaths.size(), sceneName);

        FileSystem::Get().Prefetch(allPaths, [manifest, sceneName, shouldClear, onReady, onError]() {
            spdlog::info("SceneTransition: loading '{}'", sceneName);

            if (shouldClear)
                ClearScene();

            try {
                if (!manifest.textures.empty())
                    AssetManager::Get().LoadTextures(manifest.textures);

                if (!manifest.shaders.empty())
                    AssetManager::Get().LoadShaders(manifest.shaders);

            } catch (const std::exception& e) {
                spdlog::error("SceneTransition: load failed: {}", e.what());
                if (onError) onError(e.what());
                return;
            }

            spdlog::info("SceneTransition: '{}' ready", sceneName);
            if (onReady) onReady();
        });
    });
}

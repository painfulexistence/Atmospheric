#include "scene_transition.hpp"
#include "application.hpp"
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
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
        for (const auto& path : manifest.textures) {
            std::string p = (path.size() >= 2 && path[0] == '.' && path[1] == '/') ? path.substr(2) : path;
            if (p.find("aim.png") != std::string::npos || p.find("heightmap") != std::string::npos) {
                allPaths.push_back(p);
                continue;
            }
            size_t extPos = p.find_last_of('.');
            if (extPos != std::string::npos) {
                std::string ext = p.substr(extPos);
                if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                    allPaths.push_back(p.substr(0, extPos) + ".ktx2");
                    continue;
                }
            }
            allPaths.push_back(p);
        }
#else
        allPaths.insert(allPaths.end(), manifest.textures.begin(), manifest.textures.end());
#endif

        if (Application::Get() && Application::Get()->GetConfig().useDefaultTextures) {
#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
            allPaths.push_back("assets/textures/default_diff.ktx2");
            allPaths.push_back("assets/textures/default_norm.ktx2");
            allPaths.push_back("assets/textures/default_ao.ktx2");
            allPaths.push_back("assets/textures/default_rough.ktx2");
            allPaths.push_back("assets/textures/default_metallic.ktx2");
#else
            allPaths.push_back("assets/textures/default_diff.jpg");
            allPaths.push_back("assets/textures/default_norm.jpg");
            allPaths.push_back("assets/textures/default_ao.jpg");
            allPaths.push_back("assets/textures/default_rough.jpg");
            allPaths.push_back("assets/textures/default_metallic.jpg");
#endif
        }
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
                if (Application::Get() && Application::Get()->GetConfig().useDefaultTextures) {
                    AssetManager::Get().LoadDefaultTextures();
                }

                if (!manifest.textures.empty())
                    AssetManager::Get().LoadTextures(manifest.textures);

                if (!manifest.shaders.empty())
                    AssetManager::Get().LoadShaders(manifest.shaders);

            } catch (const std::exception& e) {
                spdlog::error("SceneTransition: load failed: {}", e.what());
                if (onError) onError(e.what());
                return;
            }

            // Drop the FileSystem cache — every asset for this scene has been
            // either ConsumeSync'd into a GPU texture or read+parsed into a
            // shader/script. Keeping the raw bytes around just bloats WASM heap
            // across scene transitions. Also unlinks the MEMFS shadow copies
            // for text/audio assets so stale entries don't linger.
            FileSystem::Get().ClearCache();

            spdlog::info("SceneTransition: '{}' ready", sceneName);
            if (onReady) onReady();
        });
    });
}

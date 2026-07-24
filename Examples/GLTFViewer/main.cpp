// GLTFViewer — fly-camera viewer for glTF/GLB models through the unified prefab
// import line (ImportPrefab → Instantiate), the sibling of USDViewer. glTF has
// no build flag (it's always available), so this always builds — native and web.
//
// Two demo assets:
//   • assets/models/cube.gltf — a tiny committed PBR sample (brushed copper),
//     declared from the scene JSON's "prefab" field; always renders.
//   • assets/models/<Khronos sample>.gltf — the real PBR / transmission test
//     models. Not committed (large); fetch with scripts/downloadGLTFSamples.sh,
//     and OnLoad imports one when present.
//
// A model-picker HUD (RmlUi) lists the glTF/GLB files under assets/models and
// loads the one you choose at runtime, without recompiling — handy for eyeballing
// imported models (e.g. Mixamo skinned characters). The list comes from the
// engine VFS (FileSystem::List), so it works the same on native and web. See
// Examples/common/model_picker.hpp.
//
// This is a STATIC PBR viewer: full node hierarchy, PBR materials + textures,
// and the KHR_lights_punctual / texture_transform / emissive_strength /
// transmission / volume / ior extensions import. Known gaps (a model using
// these still loads, but the feature is skipped): morph targets, Draco / meshopt
// compression (geometry comes in empty), KTX2 textures.
//
// Controls: WASD + mouse — the engine's CameraController3D (assets/scenes/main.json).
#include "Atmospheric.hpp"
#include "../common/model_picker.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <SDL3/SDL_main.h>
#endif

class GLTFViewer : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    // Import + spawn one model file; logs a mesh/material/vertex summary.
    bool LoadModelFile(const std::string& path, const glm::vec3& position) {
        Prefab prefab = ImportPrefab(path);
        if (!prefab.ok) {
            ConsoleSubsystem::Get()->Warn("Failed to import " + path);
            return false;
        }
        GameObject* go = Instantiate(prefab, nullptr, path);
        if (go) go->SetPosition(position);
        size_t verts = 0;
        for (const auto& md : prefab.meshes)
            verts += md.vertices.size();
        ConsoleSubsystem::Get()->Info(
            path + ": " + std::to_string(prefab.meshes.size()) + " meshes, "
            + std::to_string(prefab.materials.size()) + " materials, " + std::to_string(verts) + " verts, "
            + std::to_string(prefab.skeletons.size()) + " skeletons, " + std::to_string(prefab.skeletonClips.size())
            + " clips"
        );
        return true;
    }

    void OnLoad() override {
        if (!_selectedModel.empty()) {
            // A runtime pick replaces the demo model, centred at the origin.
            LoadModelFile(_selectedModel, glm::vec3(0.0f));
        } else {
            // The committed cube.gltf arrives via the scene JSON's "prefab" field.
            // Optional Khronos sample models load here with a fallback hint.
            // Ordered by preference; the first present one is shown beside the cube.
            const char* candidates[] = {
                "assets/models/DamagedHelmet.glb",
                "assets/models/DragonAttenuation.glb",
                "assets/models/BoomBox.glb",
            };
            bool loaded = false;
            for (const char* path : candidates) {
                if (!FileSystem::Get().Exists(path)) continue;
                if (LoadModelFile(path, glm::vec3(2.5f, 0.0f, 0.0f))) {
                    loaded = true;
                    break;
                }
            }
            if (!loaded)
                ConsoleSubsystem::Get()->Info(
                    "No Khronos sample found — run scripts/downloadGLTFSamples.sh, or pick one "
                    "from the Models dropdown (showing the committed sample cube for now)"
                );
        }
        SetupPickerHud();
    }

    void SetupPickerHud() {
        std::vector<std::string> files = FileSystem::Get().List("assets/models", "gltf;glb", true);
        std::function<void(std::string)> onPick = [this](std::string path) { _pendingModel = std::move(path); };
        _hud = static_cast<ModelPickerHud*>(
            CreateGameObject()->AddComponent<ModelPickerHud>(std::move(files), _selectedModel, std::move(onPick))
        );
    }

    void OnUpdate(float, float) override {
        // The <select> "change" callback stashes a path; apply it here (not from
        // inside the UI event) so reloading the scene doesn't tear the document
        // down mid-dispatch. Reloading re-runs OnLoad, which loads the pick.
        if (!_pendingModel.empty()) {
            _selectedModel = std::move(_pendingModel);
            _pendingModel.clear();
            GoScene("main", [this] { OnLoad(); });
        }
    }

    std::string _selectedModel;
    std::string _pendingModel;
    ModelPickerHud* _hud = nullptr;
};

int main(int, char*[]) {
    GLTFViewer game(
        {
            .windowTitle = "GLTFViewer",
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}

// USDViewer — fly-camera viewer for USD scenes through the unified prefab
// import line (ImportPrefab → Instantiate). USD is a first-class format, so
// this builds by default on every platform (AE_USE_TINYUSDZ is ON), web
// included: the committed cube.usda is preloaded into MEMFS and rendered in the
// browser. Kitchen_set stays native-only (too large / too many external refs
// for a web payload).
//
// Two demo assets:
//   • assets/models/cube.usda — tiny committed sample, declared straight from the
//     scene JSON via the "prefab" entity field (always present).
//   • assets/models/kitchen/Kitchen_set.usd — Pixar's Kitchen_set, the real
//     composition stress test. Not committed (large); fetch it with
//     scripts/downloadUSDSamples.sh, and OnLoad imports it when present.
//     Kitchen_set is Z-up in cm — the importer's stage upAxis/metersPerUnit
//     handling orients and scales it automatically.
//
// A model-picker HUD (RmlUi) lists the .usd/.usda/.usdc/.usdz files under
// assets/models and loads the one you choose at runtime, without recompiling.
// The list comes from the engine VFS (FileSystem::List), so it works the same on
// native and web. See Examples/common/model_picker.hpp.
//
// Controls: WASD + mouse — the engine's CameraController3D (declared in
// assets/scenes/main.json).
#include "Atmospheric.hpp"
#include "../common/model_picker.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <SDL3/SDL_main.h>
#endif

class USDViewer : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    // Import + spawn one USD file; logs a mesh/material/vertex summary.
    bool LoadModelFile(const std::string& path) {
        Prefab prefab = ImportPrefab(path);
        if (!prefab.ok) {
            ConsoleSubsystem::Get()->Warn("Failed to import " + path);
            return false;
        }
        Instantiate(prefab, nullptr, path);
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
            // A runtime pick replaces the demo model. The importer applies the
            // stage's upAxis / metersPerUnit, so no manual scale is assumed here.
            LoadModelFile(_selectedModel);
            SetupPickerHud();
            return;
        }

        // The committed cube.usda arrives via the scene JSON's "prefab" field.
        // The kitchen is optional and big, so it loads here with a fallback hint.
        const std::string kitchen = "assets/models/kitchen/Kitchen_set.usd";
        if (FileSystem::Get().Exists(kitchen)) {
            Prefab prefab = ImportPrefab(kitchen);
            if (prefab.ok) {
                GameObject* root = Instantiate(prefab, nullptr, "Kitchen_set");
                // Kitchen_set is authored in centimetres but omits the
                // `metersPerUnit` stage metadatum, so tinyusdz reports the 1.0
                // default and the importer applies no unit scale — the set comes
                // in 100x too large. Scale the instance to metres here. (SetScale
                // recomposes from the decomposed transform, preserving the
                // importer's Z-up -> Y-up root rotation.)
                if (root) root->SetScale(glm::vec3(0.01f));
                size_t verts = 0;
                for (const auto& md : prefab.meshes)
                    verts += md.vertices.size();
                ConsoleSubsystem::Get()->Info(
                    "Kitchen_set: " + std::to_string(prefab.meshes.size()) + " meshes, "
                    + std::to_string(prefab.materials.size()) + " materials, " + std::to_string(verts) + " verts"
                );
            }
        } else {
            ConsoleSubsystem::Get()->Info(
                "Kitchen_set not found — run scripts/downloadUSDSamples.sh, or pick one "
                "from the Models dropdown (showing the committed sample cube for now)"
            );
        }
        SetupPickerHud();
    }

    void SetupPickerHud() {
        std::vector<std::string> files = FileSystem::Get().List("assets/models", "usd;usda;usdc;usdz", true);
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
    USDViewer game(
        {
            .windowTitle = "USDViewer",
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}

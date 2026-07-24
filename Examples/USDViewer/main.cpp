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
// assets/models and swaps to the one you choose at runtime. The list comes from
// the engine VFS (FileSystem::List). Picking destroys the current model and
// instantiates the new one in place (no scene reload), so the HUD and prior
// models don't pile up. See Examples/common/model_picker.hpp.
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

    // Import + spawn one USD file; logs a summary and returns the spawned root
    // (nullptr on failure) so it can later be swapped out via DestroyGameObject.
    GameObject* LoadModelFile(const std::string& path) {
        Prefab prefab = ImportPrefab(path);
        if (!prefab.ok) {
            ConsoleSubsystem::Get()->Warn("Failed to import " + path);
            return nullptr;
        }
        GameObject* go = Instantiate(prefab, nullptr, path);
        size_t verts = 0;
        for (const auto& md : prefab.meshes)
            verts += md.vertices.size();
        ConsoleSubsystem::Get()->Info(
            path + ": " + std::to_string(prefab.meshes.size()) + " meshes, "
            + std::to_string(prefab.materials.size()) + " materials, " + std::to_string(verts) + " verts, "
            + std::to_string(prefab.skeletons.size()) + " skeletons, " + std::to_string(prefab.skeletonClips.size())
            + " clips"
        );
        return go;
    }

    // Runs once (from OnInit's GoScene). cube.usda arrives via the scene JSON's
    // "prefab" field; here we load the optional Kitchen_set as the initial
    // swappable model, then build the persistent picker HUD.
    void OnLoad() override {
        std::string initial;
        const std::string kitchen = "assets/models/kitchen/Kitchen_set.usd";
        if (FileSystem::Get().Exists(kitchen)) {
            _modelRoot = LoadModelFile(kitchen);
            // Kitchen_set is authored in centimetres but omits the
            // `metersPerUnit` stage metadatum, so tinyusdz reports the 1.0
            // default and the importer applies no unit scale — it comes in 100x
            // too large. Scale to metres. (SetScale recomposes from the
            // decomposed transform, preserving the Z-up -> Y-up root rotation.)
            if (_modelRoot) {
                _modelRoot->SetScale(glm::vec3(0.01f));
                initial = kitchen;
            }
        } else {
            ConsoleSubsystem::Get()->Info(
                "Kitchen_set not found — run scripts/downloadUSDSamples.sh, or pick one "
                "from the Models dropdown (showing the committed sample cube for now)"
            );
        }
        SetupPickerHud(initial);
    }

    void SetupPickerHud(const std::string& current) {
        std::vector<std::string> files = FileSystem::Get().List("assets/models", "usd;usda;usdc;usdz", true);
        std::function<void(std::string)> onPick = [this](std::string path) { _pendingModel = std::move(path); };
        _hud = static_cast<ModelPickerHud*>(
            CreateGameObject()->AddComponent<ModelPickerHud>(std::move(files), current, std::move(onPick))
        );
    }

    void OnUpdate(float, float) override {
        if (_pendingModel.empty()) return;
        std::string path = std::move(_pendingModel);
        _pendingModel.clear();
        // Defer the swap to the next-frame flush point (safe entity mutation):
        // drop the current model, instantiate the pick in its place. The HUD
        // persists, so nothing accumulates and the scene is not reloaded.
        DeferSpawn([this, path] {
            if (_modelRoot) DestroyGameObject(_modelRoot);
            _modelRoot = LoadModelFile(path);
        });
    }

    GameObject* _modelRoot = nullptr;
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

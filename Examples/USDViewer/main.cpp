// USDViewer — fly-camera viewer for USD scenes through the unified prefab
// import line (ImportPrefab → Instantiate). USD is a first-class format, so
// this builds by default on every platform (AE_USE_TINYUSDZ is ON).
//
// Assets under assets/models/:
//   • cube.usda — tiny committed sample; just one entry in the picker.
//   • kitchen/Kitchen_set.usd — Pixar's Kitchen_set, the real composition stress
//     test. Not committed (large); fetch with scripts/downloadUSDSamples.sh.
//     Z-up in cm — the importer orients it; the viewer scales it to metres.
//
// A model-picker HUD (RmlUi) lists the .usd/.usda/.usdc/.usdz files under
// assets/models (from the engine VFS, FileSystem::List) and swaps to the one you
// choose. The scene holds only a camera + light; the model lives in one
// swappable slot, so picking destroys the current model and instantiates the new
// one in place — no scene reload, nothing piles up. A failed import just leaves
// the slot empty (no crash). See Examples/common/model_picker.hpp.
//
// Controls: WASD + mouse — the engine's CameraController3D (declared in
// assets/scenes/main.json).
#include "../common/model_picker.hpp"
#include "Atmospheric.hpp"
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
            path + ": " + std::to_string(prefab.meshes.size()) + " meshes, " + std::to_string(prefab.materials.size())
            + " materials, " + std::to_string(verts) + " verts, " + std::to_string(prefab.skeletons.size())
            + " skeletons, " + std::to_string(prefab.skeletonClips.size()) + " clips"
        );
        return go;
    }

    // Replace the on-screen model with `path`. A failed import leaves the slot
    // empty rather than substituting anything.
    void SwapTo(const std::string& path) {
        if (_modelRoot) {
            DestroyGameObject(_modelRoot);
            _modelRoot = nullptr;
        }
        _modelRoot = LoadModelFile(path);
        if (!_modelRoot) {
            _currentPath.clear();
            return;
        }
        _currentPath = path;
        // Kitchen_set is authored in centimetres but omits the `metersPerUnit`
        // stage metadatum, so tinyusdz reports the 1.0 default and the importer
        // applies no unit scale — it comes in 100x too large. Scale to metres.
        // (SetScale recomposes from the decomposed transform, preserving the
        // importer's Z-up -> Y-up root rotation.)
        if (path == "assets/models/kitchen/Kitchen_set.usd") _modelRoot->SetScale(glm::vec3(0.01f));
    }

    // Runs once (from OnInit's GoScene). Shows the first model in the list as a
    // default (cube, when it's the only one present) and builds the picker HUD.
    void OnLoad() override {
        std::vector<std::string> files = FileSystem::Get().List("assets/models", "usd;usda;usdc;usdz", 1);
        if (!files.empty()) SwapTo(files.front());
        std::function<void(std::string)> onPick = [this](std::string path) { _pendingModel = std::move(path); };
        _hud = static_cast<ModelPickerHud*>(
            CreateGameObject()->AddComponent<ModelPickerHud>(std::move(files), _currentPath, std::move(onPick))
        );
    }

    void OnUpdate(float, float) override {
        if (_pendingModel.empty()) return;
        std::string path = std::move(_pendingModel);
        _pendingModel.clear();
        // Defer the swap to the next-frame flush point (safe entity mutation).
        // The HUD persists, so nothing accumulates and the scene is not reloaded.
        DeferSpawn([this, path] { SwapTo(path); });
    }

    GameObject* _modelRoot = nullptr;
    std::string _currentPath;
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

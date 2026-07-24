// GLTFViewer — fly-camera viewer for glTF/GLB models through the unified prefab
// import line (ImportPrefab → Instantiate), the sibling of USDViewer. glTF has
// no build flag (it's always available), so this always builds — native and web.
//
// Assets under assets/models/:
//   • cube.gltf — a tiny committed PBR sample (brushed copper); just one entry
//     in the picker like any other model.
//   • <Khronos sample>.gltf/.glb — the real PBR / transmission test models. Not
//     committed (large); fetch with scripts/downloadGLTFSamples.sh.
//
// A model-picker HUD (RmlUi) lists the glTF/GLB files under assets/models (from
// the engine VFS, FileSystem::List) and swaps to the one you choose. The scene
// holds only a camera + light; the model lives in one swappable slot, so picking
// destroys the current model and instantiates the new one in place — no scene
// reload, nothing piles up. A failed import just leaves the slot empty (no
// crash). See Examples/common/model_picker.hpp.
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

    // Import + spawn one model file; logs a summary and returns the spawned root
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

    // Replace the on-screen model with `path`. A failed import leaves the slot
    // empty rather than substituting anything.
    void SwapTo(const std::string& path) {
        if (_modelRoot) {
            DestroyGameObject(_modelRoot);
            _modelRoot = nullptr;
        }
        _modelRoot = LoadModelFile(path);
        _currentPath = _modelRoot ? path : std::string{};
    }

    // Runs once (from OnInit's GoScene). Shows the first model in the list as a
    // default (cube, when it's the only one present) and builds the picker HUD.
    void OnLoad() override {
        std::vector<std::string> files = FileSystem::Get().List("assets/models", "gltf;glb", true);
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

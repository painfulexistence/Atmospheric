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
// Controls: WASD + mouse — the engine's CameraController3D (declared in
// assets/scenes/main.json).
#include "Atmospheric.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <SDL3/SDL_main.h>
#endif

class USDViewer : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
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
                APP_INFO(
                    "Kitchen_set: {} meshes, {} materials, {} verts",
                    prefab.meshes.size(),
                    prefab.materials.size(),
                    verts
                );
            }
        } else {
            APP_INFO(
                "Kitchen_set not found — run scripts/downloadUSDSamples.sh to download it "
                "(showing the committed sample cube instead)"
            );
        }
    }

    void OnUpdate(float, float) override {
    }
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

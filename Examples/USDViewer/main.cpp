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
//     scripts/fetchKitchenSet.sh, and OnLoad imports it when present.
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
                Instantiate(prefab, nullptr, "Kitchen_set");
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
                "Kitchen_set not found — run scripts/fetchKitchenSet.sh to download it "
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

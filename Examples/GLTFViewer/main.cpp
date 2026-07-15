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
// This is a STATIC PBR viewer: full node hierarchy, PBR materials + textures,
// and the KHR_lights_punctual / texture_transform / emissive_strength /
// transmission / volume / ior extensions import. Known gaps (a model using
// these still loads, but the feature is skipped): skeletal animation & morph
// targets, Draco / meshopt compression (geometry comes in empty), KTX2 textures.
//
// Controls: WASD + mouse — the engine's CameraController3D (assets/scenes/main.json).
#include "Atmospheric.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <SDL3/SDL_main.h>
#endif

class GLTFViewer : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        // The committed cube.gltf arrives via the scene JSON's "prefab" field.
        // Optional Khronos sample models load here with a fallback hint. Ordered
        // by preference; the first present one is shown, offset beside the cube.
        const char* candidates[] = {
            "assets/models/DamagedHelmet.glb",
            "assets/models/DragonAttenuation.glb",
            "assets/models/BoomBox.glb",
        };
        for (const char* path : candidates) {
            if (!FileSystem::Get().Exists(path)) continue;
            Prefab prefab = ImportPrefab(path);
            if (!prefab.ok) continue;
            GameObject* go = Instantiate(prefab, nullptr, path);
            if (go) go->SetPosition(glm::vec3(2.5f, 0.0f, 0.0f));
            size_t verts = 0;
            for (const auto& md : prefab.meshes)
                verts += md.vertices.size();
            ConsoleSubsystem::Get()->Info(
                std::string(path) + ": " + std::to_string(prefab.meshes.size()) + " meshes, "
                + std::to_string(prefab.materials.size()) + " materials, " + std::to_string(verts) + " verts"
            );
            return;
        }
        ConsoleSubsystem::Get()->Info(
            "No Khronos sample found — run scripts/downloadGLTFSamples.sh to download "
            "DamagedHelmet / TransmissionTest (showing the committed sample cube instead)"
        );
    }

    void OnUpdate(float, float) override {
    }
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

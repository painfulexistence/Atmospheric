#include "Atmospheric.hpp"
#include "components.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
#include <SDL3/SDL_main.h>
#endif

// Every animated object in this demo drives itself through a component defined
// in components.hpp — OnUpdate is left with nothing but global input.
class HelloWorld : public Application {
    using Application::Application;

    FontHandle fontID;

    void OnInit() override {
        ComponentFactory::Register("RotatorComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              glm::vec3 angVel(0.0f);
              d.Read("angVel", angVel);
              return new RotatorComponent(o, angVel);
          });
        ComponentFactory::Register("OscillatorComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              glm::vec3 axis(0,1,0);
              float amp = 1.0f, freq = 1.0f, phase = 0.0f;
              d.Read("axis", axis);
              d.Read("amp", amp);
              d.Read("freq", freq);
              d.Read("phase", phase);
              return new OscillatorComponent(o, axis, amp, freq, phase);
          });
        ComponentFactory::Register("SpritePulseComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              float minA = 0.0f, maxA = 1.0f, freq = 1.0f, phase = 0.0f;
              d.Read("min", minA);
              d.Read("max", maxA);
              d.Read("freq", freq);
              d.Read("phase", phase);
              return new SpritePulseComponent(o, minA, maxA, freq, phase);
          });
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        // Load font
        fontID = GraphicsServer::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 32.0f);
        MaterialProps matProps = {
            .diffuse = { 1., 1., 1. },
            .specular = { .296648, .296648, .296648 },
            .ambient = { .25, .20725, .20725 },
            .shininess = 0.088
        };
        AssetManager::Get().CreateMaterial(matProps);

        mainCamera->gameObject->SetPosition(glm::vec3(-10.0, 5.0, 0.0));

        // === Rotating, bobbing cube ===
        auto cubeMesh = AssetManager::Get().CreateCubeMesh("CubeMesh", 1.0f);
        cubeMesh->SetMaterial(AssetManager::Get().GetMaterials()[0]);

        auto* cube = CreateGameObject(glm::vec3(0.0f, 5.0f, 0.0f));
        cube->AddComponent<MeshComponent>(cubeMesh);
        cube->AddComponent<RotatorComponent>(glm::vec3(0.0f, 0.5f, 1.0f));
        // Bob along Z; phase pi/2 makes the sine read as cosine (matches the
        // original cos(time) motion).
        cube->AddComponent<OscillatorComponent>(glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, glm::half_pi<float>());
        cube->AddComponent<Text3DComponent>(Text3DProps{
            .text     = "Cube",
            .font     = fontID,
            .fontSize = 16.0f,                             // 16/32 base = scale 0.5, matches original WorldLabelComponent
            .color    = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
        });

        console.Info(fmt::format("Game fully loaded in {:.1f} seconds", GetWindowTime()));
        console.Info("Press R to reload shaders, ESC to quit");
        console.Info("3D sprites: 4 (depth tested), 2D sprites: 3 (screen space)");
    }

    void OnUpdate(float dt, float time) override {
        // All per-object animation now lives in components; OnUpdate only deals
        // with global/application-level input.
        if (input.IsKeyDown(Key::R)) {
            AssetManager::Get().ReloadShaders();
        }
        if (input.IsKeyDown(Key::ESCAPE)) {
            Quit();
        }
    }
};

#ifdef __EMSCRIPTEN__
// ─────────────────────────────────────────────────────────────────────────────
// Web / WebAssembly entry point
//
// Memory strategy
// ───────────────
// Textures are loaded dynamically via FileSystem::Prefetch (emscripten_fetch
// under the hood) rather than bundled in the --preload-file .data archive.
//
//   1. Startup heap footprint is small: the .data bundle only contains
//      shaders (a few hundred KB total), not all textures.
//
//   2. IndexedDB caching (EMSCRIPTEN_FETCH_PERSIST_FILE):
//      After the first visit the browser serves textures from IndexedDB,
//      so subsequent page-loads are near-instant and work offline.
//
// Peak WASM heap per KTX2 texture during load:
//   ktx2_file_bytes (moved from FileSystem cache by ConsumeSync)
//   + ETC2 block buffer (~4× smaller than uncompressed RGBA)
//   → both freed immediately after glCompressedTexImage2D
//
// Convert source textures with:
//   basisu -ktx2 -mipmap <src.jpg>
//   toktx --t2 --encode etc1s --mipmap <out.ktx2> <src.jpg>
// ─────────────────────────────────────────────────────────────────────────────

// All assets to prefetch — must match paths used in LoadDefaultTextures().
static const std::vector<std::string> kAssets = {
    "assets/textures/default_diff.ktx2",
    "assets/textures/default_norm.ktx2",
    "assets/textures/default_ao.ktx2",
    "assets/textures/default_rough.ktx2",
    "assets/textures/default_metallic.ktx2",
};

static void StartGame();

int main(int argc, char* argv[]) {
    // Fetch all assets into the FileSystem cache + IndexedDB.
    // main() returns immediately; Emscripten's event loop keeps the page alive.
    // StartGame() is invoked by the browser event loop once all fetches settle.
    FileSystem::Get().Prefetch(kAssets, StartGame);
    return 0;
}

static void StartGame() {
    // All KTX2 bytes are now in the FileSystem cache.
    // LoadDefaultTextures() → LoadKTX2Texture() → FileSystem::ConsumeSync()
    // pulls them out without any extra fopen() / fread().
    static HelloWorld game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run(); // installs emscripten_set_main_loop; never returns
}

#else
// ─────────────────────────────────────────────────────────────────────────────
// Native entry point (Linux / macOS / Windows / iOS / Android)
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
#if defined(__APPLE__) && TARGET_OS_IOS
    // On iOS, main() must return so UIApplicationMain can drive the run-loop
    // (SDL fires our animation callback via CADisplayLink). The game object
    // therefore has to outlive main — heap-allocate and intentionally leak;
    // the OS reclaims memory on process exit.
    auto* game = new HelloWorld({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game->Run();
    return 0;
#else
    HelloWorld game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
#endif
}
#endif

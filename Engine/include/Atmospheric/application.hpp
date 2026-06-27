#pragma once
#include "audio_manager.hpp"
#include "config.hpp"
#include "console.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "imgui.h"
#include "input.hpp"
#include "layer.hpp"
#include "physics_server.hpp"
#include "physics_server_2d.hpp"

#include "physics_server_2d.hpp"
#include "scene.hpp"

#include <memory>
#include <string>

// Forward declarations
class Window;
class GameObject;
class EditorLayer;
class VideoRecorder;
struct SceneBlueprint;
namespace Rml { class ElementDocument; }

struct FrameData {
    FrameData(uint64_t number, float time, float deltaTime) {
        this->number = number;
        this->time = time;
        this->deltaTime = deltaTime;
    };
    uint64_t number;
    float time;
    float deltaTime;
};

struct AppConfig {
    std::string windowTitle = INIT_SCREEN_TITLE;
    int windowWidth = INIT_SCREEN_WIDTH;
    int windowHeight = INIT_SCREEN_HEIGHT;
    bool windowResizable = false;
    bool windowFloating = false;
    bool fullscreen = false;
    bool vsync = true;
    bool enableAudio = true;
    bool enableGraphics3D = true;
    bool enablePhysics3D = true;
    float fixedTimeStep = FIXED_TIME_STEP;
    bool useDefaultTextures = false;
    bool useDefaultShaders = true;
};

// Drives an automated "warm up → capture → quit" sequence so every example can
// be recorded headlessly by a batch script. Enabled and configured purely
// through environment variables (see Application::ParseAutoCaptureEnv):
//   AE_CAPTURE_DURATION  capture length in seconds (presence enables capture)
//   AE_CAPTURE_WARMUP    seconds to wait before capturing (lets the scene settle)
//   AE_CAPTURE_MODE      "video" (default) or "screenshot"
//   AE_CAPTURE_OUTPUT    output file path
struct AutoCaptureConfig {
    enum class Mode { Video, Screenshot };

    bool        enabled    = false;
    Mode        mode       = Mode::Video;
    float       warmup     = 3.0f;
    float       duration   = 10.0f;
    std::string outputPath = "output/capture.mp4";
};

using EntityID = uint64_t;

class Application {
public:
    static Application* Get();
    const AppConfig& GetConfig() const { return _config; }

    explicit Application(AppConfig config = {});
    virtual ~Application();

    void Run();

    virtual void OnInit() {
    }// Load resources
    virtual void OnLoad() = 0;// Setup game objects (side effects)
    virtual void OnUpdate(float dt, float time) = 0;
    virtual void OnReload() {
    }// Reset game objects (side effects clean up and recreate)

    // Called just before a scene transition begins and just after the new scene is ready.
    // Override to customize the loading screen content (e.g. add a spinner or progress bar).
    // Base implementation fades a full-screen black overlay in and out via RmlUI.
    virtual void ShowLoadingScreen();
    virtual void HideLoadingScreen();

    uint64_t GetClock();

    GameObject* GetDefaultGameObject() {
        if (!_defaultGameObject) {
            _defaultGameObject = CreateGameObject();
            _defaultGameObject->SetName("__root__");
        }
        return _defaultGameObject;
    }

    void PushLayer(Layer* layer);

    inline const std::vector<GameObject*>& GetEntities() const {
        return _entities;
    }
    inline GraphicsServer* GetGraphicsServer() {
        return &graphics;
    }
    inline PhysicsServer* GetPhysicsServer() {
        return &physics;
    }
    inline Physics2DServer* GetPhysics2DServer() {
        return &physics2D;
    }
    inline Console* GetConsole() {
        return &console;
    }
    inline Input* GetInput() {
        return &input;
    }
    inline CameraComponent* GetMainCamera() {
        return mainCamera;
    }
    inline AudioManager* GetAudioManager() {
        return &audio;
    }
    // Shared video recorder. Drives both the automated capture sequence and the
    // manual F2 toggle in EditorLayer; both operate on this single instance.
    inline VideoRecorder* GetRecorder() {
        return _recorder.get();
    }

#ifndef NDEBUG
    bool IsShowingImGui() const;
    void SetShowImGui(bool show);
#endif

    std::shared_ptr<Window> GetWindow();
    void LoadScene(const std::string& jsonContent);
    void ReloadScene();
    void GoScene(const std::string& sceneName, std::function<void()> onReady = nullptr);

    // Additively load a JSON scene (same format as GoScene / scene.json).
    // Creates a container GameObject named after the scene's "name" field and
    // parents it to __root__, then populates it with the scene's entities.
    // If a scene with the same name is already loaded it is replaced in place.
    // Sets GetLastLoadedScene() / GetEditorSceneError() when done.
    void AddScene(const std::string& json);

    // Destroy the named scene container and all its descendants.
    // No-op if the scene is not currently loaded.
    void UnloadScene(const std::string& name);

    // Returns a JSON array of the names of all currently loaded scenes
    // (i.e. direct children of __root__).
    std::string GetLoadedScenes() const;

    // Name of the scene most recently loaded by AddScene,
    // or "" if the last operation failed or no scene has been loaded yet.
    std::string GetLastLoadedScene() const;

    // Load a CSB (FlatBuffers binary) scene from a memory buffer.
    // Clears the current scene and creates GameObjects from the buffer.
    // Safe to call from any thread via DeferSpawn; designed for the JS editor
    // bridge but available on all platforms.
    void LoadEditorScene(const uint8_t* data, size_t len);

    // Last error from LoadEditorScene / AddScene, or "" on success.
    // Surfaced to the JS editor bridge via ae_get_scene_error().
    const std::string& GetEditorSceneError() const;

    GameObject* CreateGameObject(
      glm::vec3 position = glm::vec3(0.0f), glm::vec3 rotation = glm::vec3(0.0f), glm::vec3 scale = glm::vec3(1.0f)
    );

    GameObject* CreateGameObject(glm::vec2 position, float rotation = 0.0f);

    // Queue a factory lambda to run at the start of the next frame, outside any
    // entity-tick loop. Use this from Component::OnTick to safely create new
    // GameObjects (CreateGameObject is not safe to call mid-iteration).
    void DeferSpawn(std::function<void()> cmd);

    // Internal: clear all scene containers (children of __root__) without
    // touching __root__ itself or other persistent entities.
    void ClearScenes();

protected:
    // These subsystems will be game accessible
    AudioManager audio;
    PhysicsServer physics;
    Physics2DServer physics2D;
    Console console;
    Input input;

    GraphicsServer graphics;

    std::vector<Scene> scenes;
    CameraComponent* mainCamera = nullptr;
    LightComponent* mainLight = nullptr;

    void Quit();


    float GetWindowTime();
    void SetWindowTime(float time);

    std::string GetWindowTitle();
    void SetWindowTitle(const std::string& title);

    template<typename T> std::shared_ptr<T> AddSubsystem() {
        static_assert(std::is_base_of<Server, T>::value, "Type T must be a subclass of Server");
        auto subsystem = std::make_shared<T>();
        _subsystems.push_back(subsystem);
        return subsystem;
    }

private:
    static Application* s_instance;

    void RegisterComponents();

    // Phase 2a: upload GPU resources declared in the blueprint.  Must run on the main thread.
    void LoadSceneResources(const SceneBlueprint& bp);
    // Phase 2b: create GameObjects + Components from resolved entity blueprints.
    void InstantiateScene(const SceneBlueprint& bp);

    AppConfig _config;

    std::shared_ptr<Window> _window = nullptr;
    std::vector<std::shared_ptr<Server>> _subsystems;
    bool _initialized = false;

    uint64_t _clock = 0;
    uint16_t _sceneIndex = 0;
    std::string _currentSceneName;
    std::string _editorSceneError;
    std::string _lastLoadedScene;
    bool _sceneReady = false;
    std::vector<GameObject*> _entities;
    EntityID _nextEntityID = 0;
    GameObject* _defaultGameObject = nullptr;

    std::vector<Layer*> _layers;
    std::vector<std::function<void()>> _spawnQueue;
    GameObject* _selectedEntity = nullptr;
#ifndef NDEBUG
    EditorLayer* _editorLayer = nullptr;
#endif

    void Update(const FrameData& frame);
    void Render(const FrameData& frame
    );// TODO: Properly separate rendering and drawing logic if the backend supports command buffering
    void SyncTransformWithPhysics();

    // ─── Loading screen ─────────────────────────────────────────────────
    Rml::ElementDocument* _loadingDoc = nullptr;
    float _loadingScreenFadeOutTimer = 0.0f;
    static constexpr float kLoadingScreenFadeDuration = 0.5f;

    // ─── Automated capture ──────────────────────────────────────────────
    std::unique_ptr<VideoRecorder> _recorder;
    AutoCaptureConfig _autoCap;
    enum class CaptureState { Idle, Warmup, Capturing, Finishing, Done };
    CaptureState _capState = CaptureState::Idle;
    float _capPhaseStart = -1.0f;// window time when the current phase began
    int _screenshotIndex = 0;
    float _nextShotTime = 0.0f;

    void ParseAutoCaptureEnv();
    void UpdateAutoCapture();// state machine, ticked once per rendered frame
    void SaveScreenshot(const std::string& path);
};

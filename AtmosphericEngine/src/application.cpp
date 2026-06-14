#include "application.hpp"
#include "animator_2d.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "component_registry.hpp"
#include "game_layer.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "light_component.hpp"
#include "mesh_component.hpp"
#include "rigidbody_2d_component.hpp"
#include "rigidbody_component.hpp"
#include "rmlui_manager.hpp"
#include "scene.hpp"
#include "scene_transition.hpp"
#include "shape_renderer_component.hpp"
#include "sprite_3d_component.hpp"
#include "sprite_component.hpp"
#include "text_component.hpp"
#include "action_manager.hpp"
#include "action.hpp"
#include "file_system.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "terrain_component.hpp"
#include "transform_component.hpp"
#include "window.hpp"
#ifndef NDEBUG
#include "editor_layer.hpp"
#endif
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#ifdef __EMSCRIPTEN__
#include <malloc.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

Application::Application(AppConfig config) : _config(config) {
    // setbuf(stdout, NULL); // Cancel output stream buffering so that output can be seen immediately

    _window = std::make_shared<Window>(WindowProps{
      .title = config.windowTitle,
      .width = config.windowWidth,
      .height = config.windowHeight,
      .resizable = config.windowResizable,
      .floating = config.windowFloating,
      .fullscreen = config.fullscreen,
      .vsync = config.vsync,
    });// Multi-window not supported now
    _window->Init();
    _window->InitImGui();

    PushLayer(new GameLayer(this));
#ifndef NDEBUG
    auto* editorLayer = new EditorLayer(this);
    _editorLayer = editorLayer;
    PushLayer(editorLayer);
#endif

    RegisterComponents();
}

void Application::RegisterComponents() {
    ComponentRegistry::Register("TransformComponent",
      [](GameObject* o, const void* /*p*/) -> Component* {
          return new TransformComponent(o, {0,0,0}, {0,0,0}, {1,1,1});
      });

    ComponentRegistry::Register("SpriteComponent",
      [](GameObject* o, const void* p) -> Component* {
          const SpriteProps defaults{};
          return new SpriteComponent(o, p ? *static_cast<const SpriteProps*>(p) : defaults);
      });

    ComponentRegistry::Register("CameraComponent",
      [](GameObject* o, const void* p) -> Component* {
          const CameraProps defaults{};
          return new CameraComponent(o, p ? *static_cast<const CameraProps*>(p) : defaults);
      });

    ComponentRegistry::Register("LightComponent",
      [](GameObject* o, const void* p) -> Component* {
          const LightProps defaults{LightType::Directional, {0.1f,0.1f,0.1f}, {1,1,1}, {1,1,1}, {0,-1,0}, {1,0,0}, 1.0f, false};
          return new LightComponent(o, p ? *static_cast<const LightProps*>(p) : defaults);
      });

    ComponentRegistry::Register("RigidbodyComponent",
      [](GameObject* o, const void* p) -> Component* {
          const RigidbodyProps defaults{};
          const auto& props = p ? *static_cast<const RigidbodyProps*>(p) : defaults;
          return new RigidbodyComponent(o, props.shape, props.mass, props.linearFactor, props.angularFactor);
      });

    ComponentRegistry::Register("Rigidbody2DComponent",
      [](GameObject* o, const void* p) -> Component* {
          const Rigidbody2DProps defaults{};
          return new Rigidbody2DComponent(o, p ? *static_cast<const Rigidbody2DProps*>(p) : defaults);
      });

    ComponentRegistry::Register("ShapeRendererComponent",
      [](GameObject* o, const void* p) -> Component* {
          const ShapeRendererProps defaults{};
          return new ShapeRendererComponent(o, p ? *static_cast<const ShapeRendererProps*>(p) : defaults);
      });

    ComponentRegistry::Register("Animator2D",
      [](GameObject* o, const void* /*p*/) -> Component* {
          return new Animator2D(o);
      });
}

Application::~Application() {
    ENGINE_LOG("Exiting...");
    _window->DeinitImGui();

    for (const auto& go : _entities)
        delete go;

    for (auto* layer : _layers) {
        layer->OnDetach();
        delete layer;
    }
}

void Application::Run() {
#ifdef TRACY_ENABLE
    TracyNoop;
    tracy::InitCallstack();
#endif
    console.Init(this);
    input.Init(this);
    audio.Init(this);
    graphics.Init(this);
    physics.Init(this);// Note that physics debug drawer is dependent on graphics server
    physics2D.Init(this);
    for (auto& subsystem : _subsystems) {
        subsystem->Init(this);
    }
    ENGINE_LOG("Subsystems initialized.");

    auto windowSize = _window->GetFramebufferSize();
    RmlUiManager::Get()->Initialize(windowSize.width, windowSize.height, graphics.renderer);

    OnInit();

    _window->MainLoop([this](float currTime, float deltaTime) {
        FrameData currFrame = { GetClock(), currTime, deltaTime };
#ifdef TRACY_ENABLE
        FrameMark;
#endif
#if SINGLE_THREAD || (defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__))
        // Single-threaded build or non-pthread web: update and render serially.
        Update(currFrame);
        Render(currFrame);
#else
        std::thread fork(&Application::Update, this, currFrame);
        Render(currFrame);
        fork.join();
#endif
        _clock++;
    });

    RmlUiManager::Get()->Shutdown();
}

void Application::PushLayer(Layer* layer) {
    _layers.push_back(layer);
    layer->OnAttach();
}

#ifndef NDEBUG
bool Application::IsShowingImGui() const {
    return _editorLayer ? _editorLayer->IsVisible() : false;
}

void Application::SetShowImGui(bool show) {
    if (_editorLayer) _editorLayer->SetVisible(show);
}
#endif

void Application::LoadScene(const SceneDef& scene) {
    ENGINE_LOG("Loading scene...");

    if (_config.useDefaultTextures) {
        AssetManager::Get().LoadDefaultTextures();
    }
    AssetManager::Get().LoadTextures(scene.textures);
    ENGINE_LOG("Textures created.");

    if (_config.useDefaultShaders) {
        AssetManager::Get().LoadDefaultShaders();
    }
    AssetManager::Get().LoadShaders(scene.shaders);
    ENGINE_LOG("Shaders created.");

    AssetManager::Get().LoadMaterials(scene.materials);
    ENGINE_LOG("Materials created.");

    for (const auto& go : scene.gameObjects) {
        auto entity = CreateGameObject(go.position, go.rotation, go.scale);
        entity->SetName(go.name);
        if (go.camera.has_value()) {
            entity->AddComponent<CameraComponent>(go.camera.value());
        }
        if (go.light.has_value()) {
            entity->AddComponent<LightComponent>(go.light.value());
        }
    }
    ENGINE_LOG("Game objects created.");

    mainCamera = graphics.GetMainCamera();
    mainLight = graphics.GetMainLight();

    _currentSceneDef = scene;
}

static glm::vec3 ParseVec3(const nlohmann::json& val, const glm::vec3& defaultValue) {
    if (val.is_array() && !val.empty()) {
        float x = val[0].get<float>();
        float y = val.size() >= 2 ? val[1].get<float>() : defaultValue.y;
        float z = val.size() >= 3 ? val[2].get<float>() : defaultValue.z;
        return glm::vec3(x, y, z);
    }
    return defaultValue;
}

static glm::vec2 ParseVec2(const nlohmann::json& val, const glm::vec2& defaultValue) {
    if (val.is_array() && !val.empty()) {
        float x = val[0].get<float>();
        float y = val.size() >= 2 ? val[1].get<float>() : defaultValue.y;
        return glm::vec2(x, y);
    }
    return defaultValue;
}

static glm::vec4 ParseVec4(const nlohmann::json& val, const glm::vec4& defaultValue) {
    if (val.is_array() && !val.empty()) {
        float r = val[0].get<float>();
        float g = val.size() >= 2 ? val[1].get<float>() : defaultValue.g;
        float b = val.size() >= 3 ? val[2].get<float>() : defaultValue.b;
        float a = val.size() >= 4 ? val[3].get<float>() : defaultValue.a;
        return glm::vec4(r, g, b, a);
    }
    return defaultValue;
}

static EasingType ParseEasingType(const std::string& easingStr) {
    if (easingStr == "Linear") return EasingType::Linear;
    if (easingStr == "SineIn") return EasingType::SineIn;
    if (easingStr == "SineOut") return EasingType::SineOut;
    if (easingStr == "SineInOut") return EasingType::SineInOut;
    if (easingStr == "QuadIn") return EasingType::QuadIn;
    if (easingStr == "QuadOut") return EasingType::QuadOut;
    if (easingStr == "QuadInOut") return EasingType::QuadInOut;
    if (easingStr == "CubicIn") return EasingType::CubicIn;
    if (easingStr == "CubicOut") return EasingType::CubicOut;
    if (easingStr == "CubicInOut") return EasingType::CubicInOut;
    if (easingStr == "QuartIn") return EasingType::QuartIn;
    if (easingStr == "QuartOut") return EasingType::QuartOut;
    if (easingStr == "QuartInOut") return EasingType::QuartInOut;
    if (easingStr == "QuintIn") return EasingType::QuintIn;
    if (easingStr == "QuintOut") return EasingType::QuintOut;
    if (easingStr == "QuintInOut") return EasingType::QuintInOut;
    if (easingStr == "ExpoIn") return EasingType::ExpoIn;
    if (easingStr == "ExpoOut") return EasingType::ExpoOut;
    if (easingStr == "ExpoInOut") return EasingType::ExpoInOut;
    if (easingStr == "CircIn") return EasingType::CircIn;
    if (easingStr == "CircOut") return EasingType::CircOut;
    if (easingStr == "CircInOut") return EasingType::CircInOut;
    if (easingStr == "BackIn") return EasingType::BackIn;
    if (easingStr == "BackOut") return EasingType::BackOut;
    if (easingStr == "BackInOut") return EasingType::BackInOut;
    if (easingStr == "ElasticIn") return EasingType::ElasticIn;
    if (easingStr == "ElasticOut") return EasingType::ElasticOut;
    if (easingStr == "ElasticInOut") return EasingType::ElasticInOut;
    if (easingStr == "BounceIn") return EasingType::BounceIn;
    if (easingStr == "BounceOut") return EasingType::BounceOut;
    if (easingStr == "BounceInOut") return EasingType::BounceInOut;
    return EasingType::Linear;
}

static Action* ParseAction(const nlohmann::json& val) {
    if (!val.is_object()) return nullptr;

    std::string type = val.value("type", "");
    float duration = val.value("duration", 0.0f);
    std::string easingStr = val.value("easing", "Linear");
    EasingType easing = ParseEasingType(easingStr);

    if (type == "MoveTo") {
        glm::vec3 pos = ParseVec3(val.value("position", nlohmann::json::array()), glm::vec3(0.0f));
        auto* action = new MoveTo(duration, pos);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "MoveBy") {
        glm::vec3 delta = ParseVec3(val.value("deltaPosition", nlohmann::json::array()), glm::vec3(0.0f));
        auto* action = new MoveBy(duration, delta);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "RotateTo") {
        glm::vec3 rot(0.0f);
        if (val.contains("rotation") && val["rotation"].is_array() && !val["rotation"].empty()) {
            rot.x = glm::radians(val["rotation"][0].get<float>());
            rot.y = val["rotation"].size() >= 2 ? glm::radians(val["rotation"][1].get<float>()) : 0.0f;
            rot.z = val["rotation"].size() >= 3 ? glm::radians(val["rotation"][2].get<float>()) : 0.0f;
        }
        auto* action = new RotateTo(duration, rot);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "RotateBy") {
        glm::vec3 delta(0.0f);
        if (val.contains("deltaRotation") && val["deltaRotation"].is_array() && !val["deltaRotation"].empty()) {
            delta.x = glm::radians(val["deltaRotation"][0].get<float>());
            delta.y = val["deltaRotation"].size() >= 2 ? glm::radians(val["deltaRotation"][1].get<float>()) : 0.0f;
            delta.z = val["deltaRotation"].size() >= 3 ? glm::radians(val["deltaRotation"][2].get<float>()) : 0.0f;
        }
        auto* action = new RotateBy(duration, delta);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "ScaleTo") {
        glm::vec3 scale = ParseVec3(val.value("scale", nlohmann::json::array()), glm::vec3(1.0f));
        auto* action = new ScaleTo(duration, scale);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "ColorTo") {
        glm::vec4 color = ParseVec4(val.value("color", nlohmann::json::array()), glm::vec4(1.0f));
        auto* action = new ColorTo(duration, color);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "FadeTo") {
        float alpha = val.value("alpha", 1.0f);
        auto* action = new FadeTo(duration, alpha);
        action->SetEasing(easing);
        return action;
    }
    else if (type == "Sequence") {
        std::vector<FiniteTimeAction*> seqActions;
        if (val.contains("actions") && val["actions"].is_array()) {
            for (const auto& actVal : val["actions"]) {
                Action* parsed = ParseAction(actVal);
                if (parsed) {
                    FiniteTimeAction* fta = dynamic_cast<FiniteTimeAction*>(parsed);
                    if (fta) {
                        seqActions.push_back(fta);
                    } else {
                        spdlog::warn("ParseAction: Sequence only supports FiniteTimeActions. Action ignored.");
                        delete parsed;
                    }
                }
            }
        }
        if (!seqActions.empty()) {
            return new Sequence(seqActions);
        }
    }
    else if (type == "RepeatForever") {
        if (val.contains("action")) {
            Action* parsed = ParseAction(val["action"]);
            if (parsed) {
                ActionInterval* interval = dynamic_cast<ActionInterval*>(parsed);
                if (interval) {
                    return new RepeatForever(interval);
                } else {
                    spdlog::warn("ParseAction: RepeatForever only supports ActionIntervals. Action ignored.");
                    delete parsed;
                }
            }
        }
    }
    return nullptr;
}

static void ParseEntity(Application* app, const nlohmann::json& entityVal, GameObject* parent) {
    std::string name = entityVal.value("name", "Entity");
    glm::vec3 position = ParseVec3(entityVal.value("position", nlohmann::json::array()), glm::vec3(0.0f));
    glm::vec3 rotationDegrees = ParseVec3(entityVal.value("rotation", nlohmann::json::array()), glm::vec3(0.0f));
    glm::vec3 rotation = glm::radians(rotationDegrees);
    glm::vec3 scale = ParseVec3(entityVal.value("scale", nlohmann::json::array()), glm::vec3(1.0f));
    bool active = entityVal.value("active", true);

    auto* go = app->CreateGameObject(position, rotation, scale);
    go->SetName(name);
    go->SetActive(active);
    if (parent) {
        go->parent = parent;
    }

    if (entityVal.contains("components") && entityVal["components"].is_array()) {
        for (const auto& compVal : entityVal["components"]) {
            std::string type = compVal.value("type", "");
            if (type == "SpriteComponent") {
                SpriteProps props;
                props.size = ParseVec2(compVal.value("size", nlohmann::json::array()), glm::vec2(100.0f, 100.0f));
                props.pivot = ParseVec2(compVal.value("pivot", nlohmann::json::array()), glm::vec2(0.5f, 0.5f));
                props.color = ParseVec4(compVal.value("color", nlohmann::json::array()), glm::vec4(1.0f));
                
                if (compVal.contains("texture")) {
                    std::string texPath = compVal["texture"].get<std::string>();
                    if (!texPath.empty()) {
                        GLuint texID = AssetManager::Get().GetTexture(texPath);
                        if (texID == 0) {
                            try {
                                texID = AssetManager::Get().CreateTexture(texPath);
                            } catch (const std::exception& e) {
                                spdlog::warn("Application::LoadScene: Failed to load texture '{}': {}", texPath, e.what());
                            }
                        }
                        props.textureID = static_cast<int>(texID);
                    }
                }

                if (compVal.contains("layer")) {
                    std::string layerStr = compVal["layer"].get<std::string>();
                    if (layerStr == "LAYER_BACKGROUND") props.layer = CanvasLayer::LAYER_BACKGROUND;
                    else if (layerStr == "LAYER_WORLD_BACK") props.layer = CanvasLayer::LAYER_WORLD_BACK;
                    else if (layerStr == "LAYER_WORLD") props.layer = CanvasLayer::LAYER_WORLD;
                    else if (layerStr == "LAYER_WORLD_FRONT") props.layer = CanvasLayer::LAYER_WORLD_FRONT;
                    else if (layerStr == "LAYER_EFFECTS") props.layer = CanvasLayer::LAYER_EFFECTS;
                    else if (layerStr == "LAYER_WORLD_2D") props.layer = CanvasLayer::LAYER_WORLD_2D;
                    else if (layerStr == "LAYER_UI_BACK") props.layer = CanvasLayer::LAYER_UI_BACK;
                    else if (layerStr == "LAYER_UI") props.layer = CanvasLayer::LAYER_UI;
                    else if (layerStr == "LAYER_UI_FRONT") props.layer = CanvasLayer::LAYER_UI_FRONT;
                    else if (layerStr == "LAYER_OVERLAY") props.layer = CanvasLayer::LAYER_OVERLAY;
                }

                props.flipX = compVal.value("flipX", false);
                props.flipY = compVal.value("flipY", false);
                props.zOrder = compVal.value("zOrder", 0);

                go->AddComponent<SpriteComponent>(props);
            }
            else if (type == "TextComponent") {
                TextProps props;
                props.text = compVal.value("text", "");
                props.fontPath = compVal.value("fontPath", "");
                props.fontSize = compVal.value("fontSize", 24.0f);
                props.size = ParseVec2(compVal.value("size", nlohmann::json::array()), glm::vec2(100.0f, 100.0f));
                props.pivot = ParseVec2(compVal.value("pivot", nlohmann::json::array()), glm::vec2(0.0f, 0.0f));
                props.color = ParseVec4(compVal.value("color", nlohmann::json::array()), glm::vec4(1.0f));

                std::string hAlignStr = compVal.value("hAlign", "Left");
                if (hAlignStr == "Center") props.hAlign = TextHAlignment::Center;
                else if (hAlignStr == "Right") props.hAlign = TextHAlignment::Right;
                else props.hAlign = TextHAlignment::Left;

                std::string vAlignStr = compVal.value("vAlign", "Top");
                if (vAlignStr == "Center") props.vAlign = TextVAlignment::Center;
                else if (vAlignStr == "Bottom") props.vAlign = TextVAlignment::Bottom;
                else props.vAlign = TextVAlignment::Top;

                if (compVal.contains("layer")) {
                    std::string layerStr = compVal["layer"].get<std::string>();
                    if (layerStr == "LAYER_BACKGROUND") props.layer = CanvasLayer::LAYER_BACKGROUND;
                    else if (layerStr == "LAYER_WORLD") props.layer = CanvasLayer::LAYER_WORLD;
                    else if (layerStr == "LAYER_UI") props.layer = CanvasLayer::LAYER_UI;
                    else if (layerStr == "LAYER_OVERLAY") props.layer = CanvasLayer::LAYER_OVERLAY;
                }

                props.zOrder = compVal.value("zOrder", 0);

                go->AddComponent<TextComponent>(props);
            }
            else if (type == "CameraComponent") {
                CameraProps props;
                props.isOrthographic = compVal.value("orthographic", false);
                if (props.isOrthographic) {
                    props.orthographic.width = compVal.value("width", 500.0f);
                    props.orthographic.height = compVal.value("height", 500.0f);
                    props.orthographic.nearClip = compVal.value("nearClip", -1.0f);
                    props.orthographic.farClip = compVal.value("farClip", 1.0f);
                } else {
                    props.perspective.fieldOfView = compVal.value("fieldOfView", 45.0f);
                    props.perspective.aspectRatio = compVal.value("aspectRatio", 1.333f);
                    props.perspective.nearClip = compVal.value("nearClip", 0.1f);
                    props.perspective.farClip = compVal.value("farClip", 500.0f);
                }
                props.verticalAngle = compVal.value("verticalAngle", 0.0f);
                props.horizontalAngle = compVal.value("horizontalAngle", 0.0f);
                props.eyeOffset = ParseVec3(compVal.value("eyeOffset", nlohmann::json::array()), glm::vec3(0.0f));

                go->AddComponent<CameraComponent>(props);
            }
            else if (type == "LightComponent") {
                LightProps props;
                std::string lightTypeStr = compVal.value("lightType", "Directional");
                if (lightTypeStr == "Directional") props.type = LightType::Directional;
                else if (lightTypeStr == "Point") props.type = LightType::Point;
                else if (lightTypeStr == "Spot") props.type = LightType::Spot;
                else if (lightTypeStr == "Area") props.type = LightType::Area;

                props.ambient = ParseVec3(compVal.value("ambient", nlohmann::json::array()), glm::vec3(0.1f));
                props.diffuse = ParseVec3(compVal.value("diffuse", nlohmann::json::array()), glm::vec3(1.0f));
                props.specular = ParseVec3(compVal.value("specular", nlohmann::json::array()), glm::vec3(1.0f));
                props.direction = ParseVec3(compVal.value("direction", nlohmann::json::array()), glm::vec3(0.0f, -1.0f, 0.0f));
                props.attenuation = ParseVec3(compVal.value("attenuation", nlohmann::json::array()), glm::vec3(1.0f, 0.0f, 0.0f));
                props.intensity = compVal.value("intensity", 1.0f);
                props.castShadow = compVal.value("castShadow", false);

                go->AddComponent<LightComponent>(props);
            }
            else if (type == "Animator2D") {
                auto* animator = new Animator2D(go);
                go->AddComponent(animator);

                if (compVal.contains("animations") && compVal["animations"].is_array()) {
                    for (const auto& animVal : compVal["animations"]) {
                        AnimationClip clip;
                        clip.name = animVal.value("name", "");
                        clip.loop = animVal.value("loop", true);
                        if (animVal.contains("frames") && animVal["frames"].is_array()) {
                            for (const auto& frameVal : animVal["frames"]) {
                                AnimationFrame frameObj;
                                frameObj.duration = frameVal.value("duration", 0.1f);
                                frameObj.uvMin = ParseVec2(frameVal.value("uvMin", nlohmann::json::array()), glm::vec2(0.0f, 0.0f));
                                frameObj.uvMax = ParseVec2(frameVal.value("uvMax", nlohmann::json::array()), glm::vec2(1.0f, 1.0f));
                                clip.frames.push_back(frameObj);
                            }
                        }
                        animator->AddAnimation(clip.name, clip);
                    }
                }

                if (compVal.contains("autoPlay")) {
                    std::string autoPlayClip = compVal["autoPlay"].get<std::string>();
                    if (!autoPlayClip.empty()) {
                        animator->Play(autoPlayClip);
                    }
                }
            }
            else if (type == "ActionManager") {
                auto* actionManager = new ActionManager(go);
                go->AddComponent(actionManager);

                if (compVal.contains("actions") && compVal["actions"].is_array()) {
                    for (const auto& actionVal : compVal["actions"]) {
                        Action* action = ParseAction(actionVal);
                        if (action) {
                            actionManager->RunAction(action);
                        }
                    }
                }
            }
        }
    }

    if (entityVal.contains("children") && entityVal["children"].is_array()) {
        for (const auto& childVal : entityVal["children"]) {
            ParseEntity(app, childVal, go);
        }
    }
}

void Application::LoadScene(const std::string& jsonContent) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonContent);
    } catch (const std::exception& e) {
        spdlog::error("Application::LoadScene: JSON parse error: {}", e.what());
        return;
    }

    std::string sceneName = j.value("name", "Unnamed");
    ENGINE_LOG("Loading scene '{}' from JSON...", sceneName);

    // Load textures
    if (j.contains("textures") && j["textures"].is_array()) {
        std::vector<std::string> texturesToLoad;
        for (const auto& tex : j["textures"]) {
            std::string texPath = tex.get<std::string>();
            if (AssetManager::Get().GetTexture(texPath) == 0) {
                texturesToLoad.push_back(texPath);
            }
        }
        if (!texturesToLoad.empty()) {
            if (_config.useDefaultTextures) {
                AssetManager::Get().LoadDefaultTextures();
            }
            AssetManager::Get().LoadTextures(texturesToLoad);
            ENGINE_LOG("JSON Textures created.");
        }
    }

    // Load shaders
    if (j.contains("shaders") && j["shaders"].is_object()) {
        std::unordered_map<std::string, ShaderProgramProps> shadersToLoad;
        for (auto& [name, shaderVal] : j["shaders"].items()) {
            if (AssetManager::Get().GetShader(name) != nullptr) {
                continue;
            }
            ShaderProgramProps props;
            props.vert = shaderVal.value("vert", "");
            props.frag = shaderVal.value("frag", "");
            if (shaderVal.contains("tesc")) {
                props.tesc = shaderVal["tesc"].get<std::string>();
            }
            if (shaderVal.contains("tese")) {
                props.tese = shaderVal["tese"].get<std::string>();
            }
            shadersToLoad[name] = props;
        }
        if (!shadersToLoad.empty()) {
            if (_config.useDefaultShaders) {
                AssetManager::Get().LoadDefaultShaders();
            }
            AssetManager::Get().LoadShaders(shadersToLoad);
            ENGINE_LOG("JSON Shaders created.");
        }
    }

    // Load entities recursively
    if (j.contains("entities") && j["entities"].is_array()) {
        for (const auto& entityVal : j["entities"]) {
            ParseEntity(this, entityVal, nullptr);
        }
        ENGINE_LOG("JSON Game objects created.");
    }

    mainCamera = graphics.GetMainCamera();
    mainLight = graphics.GetMainLight();
}

void Application::GoScene(const std::string& sceneName, std::function<void()> onReady)
{
    _sceneReady = false;
    SceneTransition::Go(sceneName, [this, sceneName, onReady]{
        for (auto* e : _entities) {
            if (e != _defaultGameObject) delete e;
        }
        _entities.clear();
        _nextEntityID = 0;
        if (_defaultGameObject) {
            _entities.push_back(_defaultGameObject);
            _nextEntityID = 1;
        }

        graphics.cameras.clear();
        graphics.directionalLights.clear();
        graphics.pointLights.clear();

        audio.StopAll();
        physics.Reset();

        _currentSceneName = sceneName;

        // Load scene.json!
        std::string manifestPath = "assets/scenes/" + sceneName + ".json";
        ENGINE_LOG("GoScene: Transitioning to scene '{}'...", sceneName);
        auto bytes = FileSystem::Get().ReadSync(manifestPath);
        if (!bytes.empty()) {
            ENGINE_LOG("GoScene: Found scene JSON file '{}', loading...", manifestPath);
            LoadScene(std::string(bytes.begin(), bytes.end()));
        } else {
            ENGINE_LOG("GoScene: Scene JSON file '{}' is empty or not found. Skipping JSON scene loading.", manifestPath);
        }

        if (onReady) onReady();
        _sceneReady = true;
    }, nullptr, _currentSceneName);
}

void Application::ReloadScene() {
    if (_currentSceneName.empty()) return;
    SceneTransition::Go(_currentSceneName, [this]{ OnLoad(); });
}

void Application::Quit() {
    ENGINE_LOG("Requested to quit.");
    _window->Close();
}

void Application::Update(const FrameData& props) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Application::Update");
#endif
    if (!_sceneReady) return;

    float dt = props.deltaTime;

    OnUpdate(dt, GetWindowTime());

    // ecs.Process(dt); // Note that most of the entity manipulation logic should be put there
    console.Process(dt);
    input.Process(dt);
    audio.Process(dt);
    physics.Process(dt);// TODO: Update only every entity's physics transform
    physics2D.Process(dt);
    graphics.Process(dt);
    for (auto& subsystem : _subsystems) {
        subsystem->Process(dt);
    }

#if SHOW_PROCESS_COST
    ENGINE_LOG(fmt::format("Update costs {} ms", (GetWindowTime() - time) * 1000));
#endif

    for (auto layer : _layers) {
        layer->OnUpdate(dt);
    }

    float time = GetWindowTime();

    for (auto go : _entities) {
        auto impostor = go->GetComponent<RigidbodyComponent>();
        if (impostor == nullptr) continue;
        if (impostor->IsKinematic()) continue;
        go->SyncObjectTransform(impostor->GetWorldTransform());
    }
}

void Application::Render(const FrameData& props) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Application::Render");
#endif
    float dt = props.deltaTime;
    float time = GetWindowTime();

    for (auto* layer : _layers) {
        layer->OnRender(dt);
    }

#if SHOW_RENDER_AND_DRAW_COST
    ENGINE_LOG(fmt::format("Render & draw cost {} ms", (GetWindowTime() - time) * 1000));
#endif
}

void Application::SyncTransformWithPhysics() {
}

uint64_t Application::GetClock() {
    return this->_clock;
}

std::shared_ptr<Window> Application::GetWindow() {
    return this->_window;
}

float Application::GetWindowTime() {
    return this->_window->GetTime();
}

void Application::SetWindowTime(float time) {
    this->_window->SetTime(time);
}

std::string Application::GetWindowTitle() {
    return this->_window->GetTitle();
}

void Application::SetWindowTitle(const std::string& title) {
    this->_window->SetTitle(title);
}

GameObject* Application::CreateGameObject(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
    auto e = new GameObject(this, position, rotation, scale);
    e->SetName(fmt::format("entity #{}", _nextEntityID++));
    _entities.push_back(e);
    return e;
}

GameObject* Application::CreateGameObject(glm::vec2 position, float angle) {
    auto e =
      new GameObject(this, glm::vec3(position.x, position.y, 0.0f), glm::vec3(0.0f, 0.0f, angle), glm::vec3(1.0f));
    e->SetName(fmt::format("entity #{}", _nextEntityID++));
    _entities.push_back(e);
    return e;
}

#ifdef __EMSCRIPTEN__
extern "C" {

EMSCRIPTEN_KEEPALIVE
void printWasmMemoryStats() {
    struct mallinfo mi = mallinfo();
    double mb = 1024.0 * 1024.0;
    size_t heapSize = emscripten_get_heap_size();
    size_t vramBytes = AssetManager::Get().getTotalTextureBytes();

    printf("========== WASM Memory Stats ==========\n");
    printf("WASM Heap Size     : %.2f MB\n", heapSize / mb);
    printf("dlmalloc Arena     : %.2f MB\n", mi.arena / mb);
    printf("Used               : %.2f MB\n", mi.uordblks / mb);
    printf("Free               : %.2f MB\n", mi.fordblks / mb);
    printf("VRAM (textures)    : %.2f MB\n", vramBytes / mb);
    printf("=======================================\n");
}

} // extern "C"
#endif

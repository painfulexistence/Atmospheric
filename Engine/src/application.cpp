#include "application.hpp"
#include "scene_loader.hpp"
#include <algorithm>
#include <unordered_set>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include "animator_2d.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "component_factory.hpp"
#include "deserializer.hpp"
#include "json_deserializer.hpp"
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
#include "transform_component.hpp"
#include "video_recorder.hpp"
#include "window.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
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

// ─── Minimal PNG writer ─────────────────────────────────────────────────────
// Self-contained so screenshots work in every build config (no FFmpeg / no
// extra dependency). Pixels are emitted with filter-type 0 and wrapped in
// uncompressed (stored) zlib blocks — larger files than a real deflate, but
// trivially correct and fast, which is all a debug screenshot needs.
namespace {

uint32_t pngCrc32(const uint8_t* buf, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void pngPutU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void pngWriteChunk(std::vector<uint8_t>& out, const char (&type)[5],
                   const std::vector<uint8_t>& data) {
    pngPutU32(out, static_cast<uint32_t>(data.size()));
    size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    pngPutU32(out, pngCrc32(out.data() + crcStart, out.size() - crcStart));
}

bool pngWrite(const std::string& path, const uint8_t* pixels, uint32_t w,
              uint32_t h, uint32_t channels) {
    if (!pixels || w == 0 || h == 0 || (channels != 3 && channels != 4))
        return false;

    // Scanlines, each prefixed by filter byte 0 (None).
    std::vector<uint8_t> raw;
    raw.reserve((static_cast<size_t>(w) * channels + 1) * h);
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint8_t* row = pixels + static_cast<size_t>(y) * w * channels;
        raw.insert(raw.end(), row, row + static_cast<size_t>(w) * channels);
    }

    // zlib stream: header + stored blocks (<=65535 bytes each) + Adler-32.
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    for (size_t pos = 0; pos < raw.size();) {
        size_t block = std::min<size_t>(65535, raw.size() - pos);
        bool last = (pos + block >= raw.size());
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(block & 0xFF));
        zlib.push_back(static_cast<uint8_t>((block >> 8) & 0xFF));
        uint16_t nlen = static_cast<uint16_t>(~block);
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + block);
        pos += block;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    pngPutU32(zlib, (b << 16) | a);

    std::vector<uint8_t> out;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    pngPutU32(ihdr, w);
    pngPutU32(ihdr, h);
    ihdr.push_back(8);                                    // bit depth
    ihdr.push_back(static_cast<uint8_t>(channels == 4 ? 6 : 2));// 6=RGBA, 2=RGB
    ihdr.push_back(0);                                    // compression
    ihdr.push_back(0);                                    // filter
    ihdr.push_back(0);                                    // interlace

    const char ihdrType[5] = "IHDR";
    const char idatType[5] = "IDAT";
    const char iendType[5] = "IEND";
    pngWriteChunk(out, ihdrType, ihdr);
    pngWriteChunk(out, idatType, zlib);
    pngWriteChunk(out, iendType, {});

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

// Cheap whole-frame statistics used to decide whether a captured frame is
// "blank" (essentially a single solid color → nothing was rendered). Also
// reports mean color so smoke captures can be eyeballed / diffed as a baseline.
struct FrameStats {
    double meanR = 0.0, meanG = 0.0, meanB = 0.0;
    int    spread = 0;   // max(max-min) across the R/G/B channels
    bool   blank  = true;
};

FrameStats analyzeFrame(const uint8_t* px, uint32_t w, uint32_t h, uint32_t channels) {
    FrameStats s;
    if (!px || w == 0 || h == 0 || channels < 3)
        return s;

    uint64_t sumR = 0, sumG = 0, sumB = 0;
    uint8_t minR = 255, minG = 255, minB = 255;
    uint8_t maxR = 0, maxG = 0, maxB = 0;
    const size_t count = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = px + i * channels;
        sumR += p[0];
        sumG += p[1];
        sumB += p[2];
        minR = std::min(minR, p[0]);
        maxR = std::max(maxR, p[0]);
        minG = std::min(minG, p[1]);
        maxG = std::max(maxG, p[1]);
        minB = std::min(minB, p[2]);
        maxB = std::max(maxB, p[2]);
    }
    s.meanR  = static_cast<double>(sumR) / count;
    s.meanG  = static_cast<double>(sumG) / count;
    s.meanB  = static_cast<double>(sumB) / count;
    s.spread = std::max({maxR - minR, maxG - minG, maxB - minB});
    s.blank  = s.spread <= 3;// tolerate dithering; a real scene varies far more
    return s;
}

}// namespace

Application* Application::s_instance = nullptr;
Application* Application::Get() { return s_instance; }

Application::Application(AppConfig config) : _config(config) {
    s_instance = this;
    // setbuf(stdout, NULL); // Cancel output stream buffering so that output can be seen immediately

#ifdef __EMSCRIPTEN__
    // Polyfill document.exitPointerLock globally to prevent crash in Safari (iOS / iframes)
    EM_ASM({
        if (typeof document !== 'undefined' && !document.exitPointerLock) {
            document.exitPointerLock = document.webkitExitPointerLock || 
                                       document.mozExitPointerLock || 
                                       function() { 
                                           console.warn("Pointer lock not supported/allowed in this browser context"); 
                                       };
        }
    });
#endif

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

    _recorder = std::make_unique<VideoRecorder>();
    ParseAutoCaptureEnv();

    RegisterComponents();
}

void Application::ParseAutoCaptureEnv() {
    auto readFloat = [](const char* key, float& out) -> bool {
        if (const char* v = std::getenv(key)) {
            out = std::strtof(v, nullptr);
            return true;
        }
        return false;
    };

    if (const char* m = std::getenv("AE_CAPTURE_MODE")) {
        _autoCap.mode = (std::strcmp(m, "screenshot") == 0)
                          ? AutoCaptureConfig::Mode::Screenshot
                          : AutoCaptureConfig::Mode::Video;
    }

    float duration = 0.0f;
    if (readFloat("AE_CAPTURE_DURATION", duration)) {
        _autoCap.duration = duration;
        _autoCap.enabled  = true;
    }
    readFloat("AE_CAPTURE_WARMUP", _autoCap.warmup);

    if (const char* o = std::getenv("AE_CAPTURE_OUTPUT")) {
        _autoCap.outputPath = o;
    } else {
        _autoCap.outputPath = (_autoCap.mode == AutoCaptureConfig::Mode::Screenshot)
                                ? "output/capture.png"
                                : "output/capture.mp4";
    }

    if (_autoCap.enabled) {
        _capState = CaptureState::Warmup;
        ENGINE_LOG("Auto-capture enabled: mode={}, warmup={}s, duration={}s, output={}",
                   _autoCap.mode == AutoCaptureConfig::Mode::Screenshot ? "screenshot" : "video",
                   _autoCap.warmup, _autoCap.duration, _autoCap.outputPath);
    }
}

// ── CanvasLayer helper ────────────────────────────────────────────────────────
static CanvasLayer ParseCanvasLayer(const std::string& s, CanvasLayer def = CanvasLayer::LAYER_WORLD) {
    if (s == "LAYER_BACKGROUND")  return CanvasLayer::LAYER_BACKGROUND;
    if (s == "LAYER_WORLD_BACK")  return CanvasLayer::LAYER_WORLD_BACK;
    if (s == "LAYER_WORLD")       return CanvasLayer::LAYER_WORLD;
    if (s == "LAYER_WORLD_FRONT") return CanvasLayer::LAYER_WORLD_FRONT;
    if (s == "LAYER_EFFECTS")     return CanvasLayer::LAYER_EFFECTS;
    if (s == "LAYER_WORLD_2D")    return CanvasLayer::LAYER_WORLD_2D;
    if (s == "LAYER_UI_BACK")     return CanvasLayer::LAYER_UI_BACK;
    if (s == "LAYER_UI")          return CanvasLayer::LAYER_UI;
    if (s == "LAYER_UI_FRONT")    return CanvasLayer::LAYER_UI_FRONT;
    if (s == "LAYER_OVERLAY")     return CanvasLayer::LAYER_OVERLAY;
    return def;
}

void Application::RegisterComponents() {
    // ── TransformComponent ────────────────────────────────────────────────────
    ComponentFactory::Register("TransformComponent",
      [](GameObject* o, Deserializer& /*d*/) -> Component* {
          return new TransformComponent(o, {0,0,0}, {0,0,0}, {1,1,1});
      });

    // ── SpriteComponent ───────────────────────────────────────────────────────
    ComponentFactory::Register("SpriteComponent",
      [](GameObject* o, Deserializer& d) -> Component* {
          SpriteProps props;
          d.Read("size",   props.size,   glm::vec2(100.0f, 100.0f));
          d.Read("pivot",  props.pivot,  glm::vec2(0.5f, 0.5f));
          d.Read("color",  props.color,  glm::vec4(1.0f));
          d.Read("flipX",  props.flipX,  false);
          d.Read("flipY",  props.flipY,  false);
          d.Read("zOrder", props.zOrder, 0);

          std::string layerStr;
          d.Read("layer", layerStr, std::string("LAYER_WORLD"));
          props.layer = ParseCanvasLayer(layerStr);

          if (d.Has("texture")) {
              std::string texPath;
              d.Read("texture", texPath);
              if (!texPath.empty()) {
                  GLuint texID = AssetManager::Get().GetTexture(texPath);
                  if (texID == 0) {
                      try { texID = AssetManager::Get().CreateTexture(texPath); }
                      catch (const std::exception& e) {
                          spdlog::warn("SpriteComponent deserializer: failed to load '{}': {}", texPath, e.what());
                      }
                  }
                  props.textureID = static_cast<int>(texID);
              }
          }

          return new SpriteComponent(o, props);
      });

    // ── TextComponent ─────────────────────────────────────────────────────────
    ComponentFactory::Register("TextComponent",
      [](GameObject* o, Deserializer& d) -> Component* {
          TextProps props;
          d.Read("text",     props.text,     std::string(""));
          d.Read("fontPath", props.fontPath, std::string(""));
          d.Read("fontSize", props.fontSize, 24.0f);
          d.Read("size",     props.size,     glm::vec2(100.0f, 100.0f));
          d.Read("pivot",    props.pivot,    glm::vec2(0.0f, 0.0f));
          d.Read("color",    props.color,    glm::vec4(1.0f));
          d.Read("zOrder",   props.zOrder,   0);

          std::string hAlignStr, vAlignStr, layerStr;
          d.Read("hAlign", hAlignStr, std::string("Left"));
          d.Read("vAlign", vAlignStr, std::string("Top"));
          d.Read("layer",  layerStr,  std::string("LAYER_WORLD"));

          if (hAlignStr == "Center") props.hAlign = TextHAlignment::Center;
          else if (hAlignStr == "Right") props.hAlign = TextHAlignment::Right;
          else props.hAlign = TextHAlignment::Left;

          if (vAlignStr == "Center") props.vAlign = TextVAlignment::Center;
          else if (vAlignStr == "Bottom") props.vAlign = TextVAlignment::Bottom;
          else props.vAlign = TextVAlignment::Top;

          props.layer = ParseCanvasLayer(layerStr);

          return new TextComponent(o, props);
      });

    // ── CameraComponent ───────────────────────────────────────────────────────
    ComponentFactory::Register("CameraComponent",
      [](GameObject* o, Deserializer& d) -> Component* {
          CameraProps props;
          d.Read("orthographic",   props.isOrthographic, false);
          d.Read("verticalAngle",  props.verticalAngle,  0.0f);
          d.Read("horizontalAngle",props.horizontalAngle,0.0f);
          d.Read("eyeOffset",      props.eyeOffset,      glm::vec3(0.0f));
          if (props.isOrthographic) {
              d.Read("width",    props.orthographic.width,    500.0f);
              d.Read("height",   props.orthographic.height,   500.0f);
              d.Read("nearClip", props.orthographic.nearClip, -1.0f);
              d.Read("farClip",  props.orthographic.farClip,   1.0f);
          } else {
              d.Read("fieldOfView",  props.perspective.fieldOfView,  45.0f);
              d.Read("aspectRatio",  props.perspective.aspectRatio,   1.333f);
              d.Read("nearClip",     props.perspective.nearClip,      0.1f);
              d.Read("farClip",      props.perspective.farClip,       500.0f);
          }
          return new CameraComponent(o, props);
      });

    // ── LightComponent ────────────────────────────────────────────────────────
    ComponentFactory::Register("LightComponent",
      [](GameObject* o, Deserializer& d) -> Component* {
          LightProps props;
          props.type       = LightType::Directional;
          props.ambient    = glm::vec3(0.1f);
          props.diffuse    = glm::vec3(1.0f);
          props.specular   = glm::vec3(1.0f);
          props.direction  = glm::vec3(0.0f, -1.0f, 0.0f);
          props.attenuation= glm::vec3(1.0f, 0.0f, 0.0f);
          props.intensity  = 1.0f;
          props.castShadow = false;

          std::string lightTypeStr;
          d.Read("lightType",  lightTypeStr, std::string("Directional"));
          d.Read("ambient",    props.ambient,    glm::vec3(0.1f));
          d.Read("diffuse",    props.diffuse,    glm::vec3(1.0f));
          d.Read("specular",   props.specular,   glm::vec3(1.0f));
          d.Read("direction",  props.direction,  glm::vec3(0.0f, -1.0f, 0.0f));
          d.Read("attenuation",props.attenuation,glm::vec3(1.0f, 0.0f, 0.0f));
          d.Read("intensity",  props.intensity,  1.0f);
          d.Read("castShadow", props.castShadow, false);

          if (lightTypeStr == "Point")  props.type = LightType::Point;
          else if (lightTypeStr == "Spot")  props.type = LightType::Spot;
          else if (lightTypeStr == "Area")  props.type = LightType::Area;

          return new LightComponent(o, props);
      });

    // ── ShapeRendererComponent ────────────────────────────────────────────────
    ComponentFactory::Register("ShapeRendererComponent",
      [](GameObject* o, Deserializer& d) -> Component* {
          ShapeRendererProps props;
          d.Read("color",     props.color,    glm::vec4(1.0f));
          d.Read("thickness", props.thickness, 1.0f);
          d.Read("filled",    props.filled,   false);
          d.Read("radius",    props.radius,   10.0f);
          d.Read("boxHalfSize",props.boxHalfSize, glm::vec2(10.0f));
          std::string layerStr;
          d.Read("layer", layerStr, std::string("LAYER_WORLD_2D"));
          props.layer = ParseCanvasLayer(layerStr, CanvasLayer::LAYER_WORLD_2D);
          return new ShapeRendererComponent(o, props);
      });

    // ── Animator2D ────────────────────────────────────────────────────────────
    // Clip data is read via ReadArray so it lives entirely inside this lambda.
    ComponentFactory::Register("Animator2D",
      [](GameObject* o, Deserializer& d) -> Component* {
          auto* animator = new Animator2D(o);
          for (auto& animD : d.ReadArray("animations")) {
              AnimationClip clip;
              animD->Read("name", clip.name, std::string(""));
              animD->Read("loop", clip.loop, true);
              for (auto& frameD : animD->ReadArray("frames")) {
                  AnimationFrame f;
                  frameD->Read("duration", f.duration, 0.1f);
                  frameD->Read("uvMin", f.uvMin, glm::vec2(0.0f));
                  frameD->Read("uvMax", f.uvMax, glm::vec2(1.0f));
                  clip.frames.push_back(f);
              }
              if (!clip.name.empty())
                  animator->AddAnimation(clip.name, clip);
          }
          std::string autoPlay;
          d.Read("autoPlay", autoPlay, std::string(""));
          if (!autoPlay.empty()) animator->Play(autoPlay);
          return animator;
      });

    // ── ActionManager ─────────────────────────────────────────────────────────
    // Action parsing is recursive and format-specific; we delegate back to the
    // existing ParseAction helper by capturing the JSON node via ReadArray.
    // This preserves all easing / Sequence / RepeatForever logic without
    // duplicating it in the lambda.
    ComponentFactory::Register("ActionManager",
      [](GameObject* o, Deserializer& d) -> Component* {
          auto* mgr = new ActionManager(o);
          // ParseAction expects a raw nlohmann::json, so we reach through the
          // Deserializer only to enumerate the top-level "actions" array.
          // Each element is reconstructed as a JSONDeserializer-owned value.
          // Since ParseAction is a file-local static function declared later in
          // this translation unit, ActionManager registration is wired up in
          // Application::RegisterActionManager() called from RegisterComponents.
          // See below for the ActionManager post-init hook.
          (void)d; // actions wired up in ParseEntity fallback for now
          return mgr;
      });
}

Application::~Application() {
    if (s_instance == this) s_instance = nullptr;
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
    _recorder->setAudioManager(&audio);
    graphics.Init(this);
    physics.Init(this);// Note that physics debug drawer is dependent on graphics server
    physics2D.Init(this);
    for (auto& subsystem : _subsystems) {
        subsystem->Init(this);
    }
    ENGINE_LOG("Subsystems initialized.");

    auto windowSize = _window->GetSize();
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

#if !(defined(__APPLE__) && TARGET_OS_IOS)
    // On iOS, MainLoop returns immediately after handing the run-loop to
    // UIApplicationMain. The OS reclaims process resources at app exit.
    RmlUiManager::Get()->Shutdown();
#endif
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
    glm::vec3 position      = ParseVec3(entityVal.value("position", nlohmann::json::array()), glm::vec3(0.0f));
    glm::vec3 rotationDegrees = ParseVec3(entityVal.value("rotation", nlohmann::json::array()), glm::vec3(0.0f));
    glm::vec3 rotation      = glm::radians(rotationDegrees);
    glm::vec3 scale         = ParseVec3(entityVal.value("scale", nlohmann::json::array()), glm::vec3(1.0f));
    bool active             = entityVal.value("active", true);

    auto* go = app->CreateGameObject(position, rotation, scale);
    go->SetName(name);
    go->SetActive(active);
    if (parent) go->parent = parent;

    if (entityVal.contains("components") && entityVal["components"].is_array()) {
        for (const auto& compVal : entityVal["components"]) {
            std::string type = compVal.value("type", "");
            if (type.empty()) continue;

            if (ComponentFactory::Has(type)) {
                // ── Registered path: delegate entirely to the component's lambda.
                JSONDeserializer d(compVal);
                ComponentFactory::Create(type, go, d);

                // Special post-init for ActionManager: the ParseAction helper is
                // a file-local static that cannot be captured in a Register lambda
                // (it is declared below in this TU).  We run the actions here, after
                // the ActionManager component has been created by the registry.
                if (type == "ActionManager") {
                    auto* mgr = go->GetComponent<ActionManager>();
                    if (mgr && compVal.contains("actions") && compVal["actions"].is_array()) {
                        for (const auto& actionVal : compVal["actions"]) {
                            if (Action* a = ParseAction(actionVal)) mgr->RunAction(a);
                        }
                    }
                }
            } else {
                spdlog::warn("ParseEntity: unregistered component type '{}' on '{}' — skipping", type, name);
            }
        }
    }

    if (entityVal.contains("children") && entityVal["children"].is_array()) {
        for (const auto& childVal : entityVal["children"])
            ParseEntity(app, childVal, go);
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
            AssetManager::Get().RecordSceneTextures(sceneName, texturesToLoad);
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
            std::vector<std::string> shaderNames;
            shaderNames.reserve(shadersToLoad.size());
            for (const auto& [name, _] : shadersToLoad) shaderNames.push_back(name);
            AssetManager::Get().RecordSceneShaders(sceneName, shaderNames);
            ENGINE_LOG("JSON Shaders created.");
        }
    }

    // Create a scene container under __root__ so scenes can be layered.
    // All entities from this JSON become children of this container.
    auto* container = CreateGameObject();
    container->SetName(sceneName);
    container->parent = GetDefaultGameObject();

    // Load entities recursively
    if (j.contains("entities") && j["entities"].is_array()) {
        for (const auto& entityVal : j["entities"]) {
            ParseEntity(this, entityVal, container);
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
        // Full scene transition: clear all scene containers, then load fresh.
        ClearScenes();

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

void Application::LoadEditorScene(const uint8_t* data, size_t len)
{
    // Mirror GoScene's entity-clearing logic without requiring a manifest file.
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

    SceneLoader loader(this);
    auto result = loader.LoadFromBuffer(data, len);
    if (!result.success) {
        _editorSceneError = result.error;
        spdlog::warn("[Editor] Scene load failed: {}", result.error);
    } else {
        _editorSceneError.clear();
        spdlog::info("[Editor] Scene loaded: {} node(s)", result.allNodes.size());
    }

    _sceneReady = true;
}

void Application::UnloadScene(const std::string& name)
{
    if (!_defaultGameObject) return;

    // Find the scene container (direct child of __root__ with this name).
    GameObject* container = nullptr;
    for (auto* e : _entities) {
        if (e != _defaultGameObject && e->parent == _defaultGameObject && e->GetName() == name) {
            container = e;
            break;
        }
    }
    if (!container) return;

    // Collect the full subtree iteratively (no children list on GameObject).
    std::unordered_set<GameObject*> toRemove;
    std::vector<GameObject*> stack = {container};
    while (!stack.empty()) {
        auto* node = stack.back(); stack.pop_back();
        toRemove.insert(node);
        for (auto* e : _entities) {
            if (e->parent == node) stack.push_back(e);
        }
    }

    _entities.erase(
        std::remove_if(_entities.begin(), _entities.end(),
            [&toRemove](GameObject* e) { return toRemove.count(e) > 0; }),
        _entities.end());
    for (auto* e : toRemove) delete e;

    // Free GPU/CPU assets that were first loaded by this scene.
    // Must come after GameObjects are deleted so no component holds a dangling ref.
    AssetManager::Get().UnloadSceneAssets(name);

    mainCamera = graphics.GetMainCamera();
    mainLight = graphics.GetMainLight();
    spdlog::info("[Scene] Unloaded scene '{}'.", name);
}

void Application::ClearScenes()
{
    if (!_defaultGameObject) return;
    // Collect names of all direct children of __root__ first to avoid
    // iterator invalidation inside UnloadScene.
    std::vector<std::string> names;
    for (auto* e : _entities) {
        if (e != _defaultGameObject && e->parent == _defaultGameObject)
            names.push_back(e->GetName());
    }
    for (const auto& n : names) UnloadScene(n);
}

void Application::AddScene(const std::string& json)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        _editorSceneError = e.what();
        _lastLoadedScene  = "";
        spdlog::warn("[Scene] AddScene JSON parse error: {}", e.what());
        return;
    }

    std::string sceneName = j.value("name", "Unnamed");
    // Replace any existing scene with the same name.
    UnloadScene(sceneName);

    try {
        LoadScene(json);
        _editorSceneError.clear();
        _lastLoadedScene = sceneName;
        spdlog::info("[Scene] Added scene '{}'.", sceneName);
    } catch (const std::exception& e) {
        _editorSceneError = e.what();
        _lastLoadedScene  = "";
        spdlog::warn("[Scene] AddScene '{}' failed: {}", sceneName, e.what());
    }
}

std::string Application::GetLoadedScenes() const
{
    nlohmann::json arr = nlohmann::json::array();
    if (_defaultGameObject) {
        for (auto* e : _entities) {
            if (e != _defaultGameObject && e->parent == _defaultGameObject)
                arr.push_back(e->GetName());
        }
    }
    return arr.dump();
}

std::string Application::GetLastLoadedScene() const
{
    return _lastLoadedScene;
}

const std::string& Application::GetEditorSceneError() const
{
    return _editorSceneError;
}

void Application::Quit() {
    ENGINE_LOG("Requested to quit.");
    _window->Close();
}

void Application::DeferSpawn(std::function<void()> cmd) {
    _spawnQueue.push_back(std::move(cmd));
}

void Application::Update(const FrameData& props) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Application::Update");
#endif
    if (!_sceneReady) return;

    // Flush deferred spawns queued by component OnTick last frame.
    // This runs before OnUpdate and GameLayer::OnUpdate, so _entities is not
    // being iterated and CreateGameObject is safe to call here.
    {
        std::vector<std::function<void()>> queue;
        queue.swap(_spawnQueue);
        for (auto& cmd : queue) cmd();
    }

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

    // Runs on the render (GL) thread, after the frame is drawn — the only safe
    // place to read back pixels for recording / screenshots.
    UpdateAutoCapture();

    // Feed the encoder one frame per render tick whenever recording is active,
    // whether it was started by the auto-capture sequence or the manual F2 key.
    if (_recorder && _recorder->isRecording())
        _recorder->captureFrame();

#if SHOW_RENDER_AND_DRAW_COST
    ENGINE_LOG(fmt::format("Render & draw cost {} ms", (GetWindowTime() - time) * 1000));
#endif
}

void Application::UpdateAutoCapture() {
    if (_capState == CaptureState::Idle || _capState == CaptureState::Done)
        return;

    float now = GetWindowTime();
    if (_capPhaseStart < 0.0f)
        _capPhaseStart = now;// lazy init: anchor to the first rendered frame
    float elapsed = now - _capPhaseStart;

    Renderer* renderer = graphics.renderer;

    auto ensureParentDir = [](const std::string& path) {
        std::error_code ec;
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
    };

    // Builds "name_000.png" style paths for a screenshot burst, or the bare
    // output path when only a single shot is requested (duration <= 0).
    auto screenshotPath = [this](int index) -> std::string {
        if (_autoCap.duration <= 0.0f)
            return _autoCap.outputPath;
        std::filesystem::path p(_autoCap.outputPath);
        std::string ext = p.extension().string();
        if (ext.empty())
            ext = ".png";
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d", index);
        return (p.parent_path() / (p.stem().string() + suffix + ext)).string();
    };

    switch (_capState) {
    case CaptureState::Warmup:
        if (elapsed >= _autoCap.warmup) {
            _capPhaseStart = now;
            ensureParentDir(_autoCap.outputPath);
            if (_autoCap.mode == AutoCaptureConfig::Mode::Video) {
                VideoRecorder::Config cfg;
                cfg.outputPath = _autoCap.outputPath;
                _recorder->startRecording(renderer, cfg);
            }
            _screenshotIndex = 0;
            _nextShotTime    = 0.0f;
            _capState        = CaptureState::Capturing;
        }
        break;

    case CaptureState::Capturing: {
        // Video frames are pumped by Render() via captureFrame(); here we only
        // drive the screenshot cadence and the overall capture-window timing.
        if (_autoCap.mode == AutoCaptureConfig::Mode::Screenshot &&
            elapsed >= _nextShotTime) {
            SaveScreenshot(screenshotPath(_screenshotIndex));
            ++_screenshotIndex;
            _nextShotTime += 1.0f;// one shot per second across the window
        }

        bool singleShotDone = _autoCap.mode == AutoCaptureConfig::Mode::Screenshot &&
                              _autoCap.duration <= 0.0f && _screenshotIndex >= 1;
        if (singleShotDone || elapsed >= _autoCap.duration)
            _capState = CaptureState::Finishing;
        break;
    }

    case CaptureState::Finishing:
        if (_autoCap.mode == AutoCaptureConfig::Mode::Video)
            _recorder->stopRecording();// joins encoder thread and flushes to disk
        _capState = CaptureState::Done;
        Quit();
        break;

    default:
        break;
    }
}

void Application::SaveScreenshot(const std::string& path) {
    if (!graphics.renderer)
        return;
    graphics.renderer->readPixelsAsync([&path](const GpuImageData& img) {
        if (img.data.empty())
            return;
        bool ok = pngWrite(path, img.data.data(), img.width, img.height, img.channelCount);
        FrameStats st = analyzeFrame(img.data.data(), img.width, img.height, img.channelCount);
        // Machine-readable marker consumed by scripts/smokeTest.sh (grepped from
        // stdout). Flushed so it survives the imminent auto-quit.
        fmt::print(
            "[Smoke] result path={} size={}x{} meanRGB={:.1f},{:.1f},{:.1f} spread={} blank={} write={}\n",
            path, img.width, img.height, st.meanR, st.meanG, st.meanB, st.spread,
            st.blank ? 1 : 0, ok ? 1 : 0);
        std::fflush(stdout);
        if (ok)
            ENGINE_LOG("Screenshot saved: {} ({}x{})", path, img.width, img.height);
        else
            spdlog::error("[Screenshot] Failed to write {}", path);
    });
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
void printMemoryStats() {
    struct mallinfo mi = mallinfo();
    double mb = 1024.0 * 1024.0;
    size_t heapSize = emscripten_get_heap_size();
    size_t vramBytes = AssetManager::Get().getTotalTextureBytes();

    double jsHeapSizeMB = -1.0;
    int jsHeapBytes = EM_ASM_INT({
        if (globalThis.performance && globalThis.performance.memory) {
            return globalThis.performance.memory.usedJSHeapSize;
        }
        return -1;
    });
    if (jsHeapBytes >= 0) {
        jsHeapSizeMB = (double)jsHeapBytes / mb;
    }

    printf("========== Memory Stats ==========\n");
    printf("WASM Heap Size     : %.2f MB\n", heapSize / mb);
    printf("dlmalloc Arena     : %.2f MB\n", mi.arena / mb);
    printf("Used               : %.2f MB\n", mi.uordblks / mb);
    printf("Free               : %.2f MB\n", mi.fordblks / mb);
    if (jsHeapSizeMB >= 0.0) {
        printf("JS Heap Size       : %.2f MB\n", jsHeapSizeMB);
    } else {
        printf("JS Heap Size       : N/A (unsupported)\n");
    }
    printf("VRAM (textures)    : %.2f MB\n", vramBytes / mb);
    printf("==================================\n");
}

} // extern "C"
#endif

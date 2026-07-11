#include "application.hpp"
#include "scene_blueprint.hpp"
#include "scene_loader.hpp"
#include <algorithm>
#include <unordered_set>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include "action.hpp"
#include "action_manager.hpp"
#include "animator_2d.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "camera_controller_3d.hpp"
#include "component_factory.hpp"
#include "deserializer.hpp"
#include "file_system.hpp"
#include "game_layer.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "json_deserializer.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "mesh_component.hpp"
#include "rigidbody_2d_component.hpp"
#include "rigidbody_component.hpp"
#include "rmlui_manager.hpp"
#include "scene.hpp"
#include "scene_transition.hpp"
#include "shape_renderer_component.hpp"
#include "sprite_3d_component.hpp"
#include "sprite_component.hpp"
#include "streaming_terrain_component.hpp"
#include "text_2d_component.hpp"
#include "text_3d_component.hpp"
#include "transform_component.hpp"
#include "ui_page_manager.hpp"
#include "video_recorder.hpp"
#include "voxel_volume_component.hpp"
#include "voxel_world_component.hpp"
#include "window.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#ifndef NDEBUG
#include "editor_layer.hpp"
#endif
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#include <malloc.h>
#endif

// ─── Minimal PNG writer ─────────────────────────────────────────────────────
// Self-contained so screenshots work in every build config (no FFmpeg / no
// extra dependency). Pixels are emitted with filter-type 0 and wrapped in
// uncompressed (stored) zlib blocks — larger files than a real deflate, but
// trivially correct and fast, which is all a debug screenshot needs.
namespace {

    uint32_t PngCrc32(const uint8_t* buf, size_t len) {
        static uint32_t gtable[256];
        static bool ginit = false;
        if (!ginit) {
            for (uint32_t n = 0; n < 256; ++n) {
                uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                gtable[n] = c;
            }
            ginit = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
            crc = gtable[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    void PngPutU32(std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>(x & 0xFF));
    }

    void PngWriteChunk(std::vector<uint8_t>& out, const char (&type)[5], const std::vector<uint8_t>& data) {
        PngPutU32(out, static_cast<uint32_t>(data.size()));
        size_t crcStart = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        PngPutU32(out, PngCrc32(out.data() + crcStart, out.size() - crcStart));
    }

    bool PngWrite(const std::string& path, const uint8_t* pixels, uint32_t w, uint32_t h, uint32_t channels) {
        if (!pixels || w == 0 || h == 0 || (channels != 3 && channels != 4)) return false;

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
            auto nlen = static_cast<uint16_t>(~block);
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
        PngPutU32(zlib, (b << 16) | a);

        std::vector<uint8_t> out;
        const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        out.insert(out.end(), sig, sig + 8);

        std::vector<uint8_t> ihdr;
        PngPutU32(ihdr, w);
        PngPutU32(ihdr, h);
        ihdr.push_back(8);// bit depth
        ihdr.push_back(static_cast<uint8_t>(channels == 4 ? 6 : 2));// 6=RGBA, 2=RGB
        ihdr.push_back(0);// compression
        ihdr.push_back(0);// filter
        ihdr.push_back(0);// interlace

        const char ihdrType[5] = "IHDR";
        const char idatType[5] = "IDAT";
        const char iendType[5] = "IEND";
        PngWriteChunk(out, ihdrType, ihdr);
        PngWriteChunk(out, idatType, zlib);
        PngWriteChunk(out, iendType, {});

        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        return true;
    }

    // Cheap whole-frame statistics used to decide whether a captured frame is
    // "blank" (essentially a single solid color → nothing was rendered). Also
    // reports mean color so smoke captures can be eyeballed / diffed as a baseline.
    struct FrameStats {
        double meanR = 0.0, meanG = 0.0, meanB = 0.0;
        int spread = 0;// max(max-min) across the R/G/B channels
        bool blank = true;
    };

    FrameStats AnalyzeFrame(const uint8_t* px, uint32_t w, uint32_t h, uint32_t channels) {
        FrameStats s;
        if (!px || w == 0 || h == 0 || channels < 3) return s;

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
        s.meanR = static_cast<double>(sumR) / count;
        s.meanG = static_cast<double>(sumG) / count;
        s.meanB = static_cast<double>(sumB) / count;
        s.spread = std::max({ maxR - minR, maxG - minG, maxB - minB });
        s.blank = s.spread <= 3;// tolerate dithering; a real scene varies far more
        return s;
    }

}// namespace

Application* Application::s_instance = nullptr;
Application* Application::Get() {
    return s_instance;
}

Application::Application(AppConfig config) : _config(config) {
    s_instance = this;
    // setbuf(stdout, NULL); // Cancel output stream buffering so that output can be seen immediately

#ifdef __EMSCRIPTEN__
    // Polyfill document.exitPointerLock globally to prevent crash in Safari (iOS / iframes)
    // clang-format off
    // NOLINTBEGIN
    EM_ASM({
        if (typeof document !== 'undefined' && !document.exitPointerLock) {
            document.exitPointerLock = document.webkitExitPointerLock || document.mozExitPointerLock || function() {
                console.warn("Pointer lock not supported/allowed in this browser context");
            };
        }
    });
    // NOLINTEND
    // clang-format on
#endif

    if (!config.headless) {
        _window = std::make_unique<Window>(WindowProps{
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
    }

    // Construct subsystems in dependency order (matches member declaration
    // order). Each registers itself as its type's Get() locator here; the real
    // GL/device setup still happens in Init(this) from Run(), once the full
    // engine is wired up.
    _console = std::make_unique<ConsoleSubsystem>();
    _input = std::make_unique<InputSubsystem>();
    _audio = std::make_unique<AudioSubsystem>();
    _graphics = std::make_unique<GraphicsSubsystem>();
    _physics = std::make_unique<Physics3DSubsystem>();
    _physics2D = std::make_unique<Physics2DSubsystem>();

    _assetManager = std::make_unique<AssetManager>();
    _rmlUi = std::make_unique<RmlUiManager>();
    _uiPages = std::make_unique<UIPageManager>();

    PushLayer(new GameLayer(this));
#ifndef NDEBUG
    if (!config.headless) {
        auto* editorLayer = new EditorLayer(this);
        _editorLayer = editorLayer;
        PushLayer(editorLayer);
    }
#endif

    _recorder = std::make_unique<VideoRecorder>();
    // Auto-capture reads back rendered frames; meaningless without a GL context.
    if (!config.headless) ParseAutoCaptureEnv();

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
        _autoCap.mode =
            (std::strcmp(m, "screenshot") == 0) ? AutoCaptureConfig::Mode::Screenshot : AutoCaptureConfig::Mode::Video;
    }

    float duration = 0.0f;
    if (readFloat("AE_CAPTURE_DURATION", duration)) {
        _autoCap.duration = duration;
        _autoCap.enabled = true;
    }
    readFloat("AE_CAPTURE_WARMUP", _autoCap.warmup);

    if (const char* o = std::getenv("AE_CAPTURE_OUTPUT")) {
        _autoCap.outputPath = o;
    } else {
        _autoCap.outputPath =
            (_autoCap.mode == AutoCaptureConfig::Mode::Screenshot) ? "output/capture.png" : "output/capture.mp4";
    }

    if (_autoCap.enabled) {
        _capState = CaptureState::Warmup;
        ENGINE_LOG(
            "Auto-capture enabled: mode={}, warmup={}s, duration={}s, output={}",
            _autoCap.mode == AutoCaptureConfig::Mode::Screenshot ? "screenshot" : "video",
            _autoCap.warmup,
            _autoCap.duration,
            _autoCap.outputPath
        );
    }
}

// ── CanvasLayer helper ────────────────────────────────────────────────────────
static CanvasLayer ParseCanvasLayer(const std::string& s, CanvasLayer def = CanvasLayer::LAYER_WORLD) {
    if (s == "LAYER_BACKGROUND") return CanvasLayer::LAYER_BACKGROUND;
    if (s == "LAYER_WORLD_BACK") return CanvasLayer::LAYER_WORLD_BACK;
    if (s == "LAYER_WORLD") return CanvasLayer::LAYER_WORLD;
    if (s == "LAYER_WORLD_FRONT") return CanvasLayer::LAYER_WORLD_FRONT;
    if (s == "LAYER_EFFECTS") return CanvasLayer::LAYER_EFFECTS;
    if (s == "LAYER_WORLD_2D") return CanvasLayer::LAYER_WORLD_2D;
    if (s == "LAYER_UI_BACK") return CanvasLayer::LAYER_UI_BACK;
    if (s == "LAYER_UI") return CanvasLayer::LAYER_UI;
    if (s == "LAYER_UI_FRONT") return CanvasLayer::LAYER_UI_FRONT;
    if (s == "LAYER_OVERLAY") return CanvasLayer::LAYER_OVERLAY;
    return def;
}

void Application::RegisterComponents() {
    // ── TransformComponent ────────────────────────────────────────────────────
    ComponentFactory::Register("TransformComponent", [](GameObject* o, Deserializer& /*d*/) -> Component* {
        return new TransformComponent(o, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 });
    });

    // ── SpriteComponent ───────────────────────────────────────────────────────
    ComponentFactory::Register("SpriteComponent", [](GameObject* o, Deserializer& d) -> Component* {
        SpriteProps props;
        d.Read("size", props.size, glm::vec2(100.0f, 100.0f));
        d.Read("pivot", props.pivot, glm::vec2(0.5f, 0.5f));
        d.Read("color", props.color, glm::vec4(1.0f));
        d.Read("flipX", props.flipX, false);
        d.Read("flipY", props.flipY, false);
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
                    try {
                        texID = AssetManager::Get().CreateTexture(texPath);
                    } catch (const std::exception& e) {
                        spdlog::warn("SpriteComponent deserializer: failed to load '{}': {}", texPath, e.what());
                    }
                }
                props.texture = texID;
            }
        }

        return new SpriteComponent(o, props);
    });

    // ── Text2DComponent ─────────────────────────────────────────────────────────
    ComponentFactory::Register("Text2DComponent", [](GameObject* o, Deserializer& d) -> Component* {
        Text2DProps props;
        d.Read("text", props.text, std::string(""));
        d.Read("fontSize", props.fontSize, 24.0f);
        d.Read("size", props.size, glm::vec2(100.0f, 100.0f));
        d.Read("pivot", props.pivot, glm::vec2(0.0f, 0.0f));
        d.Read("color", props.color, glm::vec4(1.0f));
        d.Read("zOrder", props.zOrder, 0);

        std::string fontPath = "";
        d.Read("fontPath", fontPath, std::string(""));
        if (!fontPath.empty()) {
            props.font = GraphicsSubsystem::Get()->LoadFont(fontPath, props.fontSize);
        } else {
            int fontVal = 0;
            d.Read("font", fontVal, 0);
            if (fontVal > 0) {
                props.font = FontHandle(fontVal);
            }
        }

        std::string hAlignStr, vAlignStr, layerStr;
        d.Read("hAlign", hAlignStr, std::string("Left"));
        d.Read("vAlign", vAlignStr, std::string("Top"));
        d.Read("layer", layerStr, std::string("LAYER_WORLD"));

        if (hAlignStr == "Center")
            props.hAlign = TextHAlignment::Center;
        else if (hAlignStr == "Right")
            props.hAlign = TextHAlignment::Right;
        else
            props.hAlign = TextHAlignment::Left;

        if (vAlignStr == "Center")
            props.vAlign = TextVAlignment::Center;
        else if (vAlignStr == "Bottom")
            props.vAlign = TextVAlignment::Bottom;
        else
            props.vAlign = TextVAlignment::Top;

        props.layer = ParseCanvasLayer(layerStr);

        return new Text2DComponent(o, props);
    });


    // ── Text3DComponent ─────────────────────────────────────────────────────────
    ComponentFactory::Register("Text3DComponent", [](GameObject* o, Deserializer& d) -> Component* {
        Text3DProps props;
        d.Read("text", props.text, std::string(""));
        d.Read("fontSize", props.fontSize, 24.0f);
        d.Read("offset", props.offset, glm::vec3(0.0f, 1.2f, 0.0f));
        d.Read("color", props.color, glm::vec4(1.0f));

        std::string fontPath = "";
        d.Read("fontPath", fontPath, std::string(""));
        if (!fontPath.empty()) {
            props.font = GraphicsSubsystem::Get()->LoadFont(fontPath, props.fontSize);
        } else {
            int fontVal = 0;
            d.Read("font", fontVal, 0);
            if (fontVal > 0) {
                props.font = FontHandle(fontVal);
            }
        }

        return new Text3DComponent(o, props);
    });

    // ── CameraComponent ───────────────────────────────────────────────────────
    ComponentFactory::Register("CameraComponent", [](GameObject* o, Deserializer& d) -> Component* {
        CameraProps props;
        d.Read("orthographic", props.isOrthographic, false);
        d.Read("verticalAngle", props.verticalAngle, 0.0f);
        d.Read("horizontalAngle", props.horizontalAngle, 0.0f);
        d.Read("eyeOffset", props.eyeOffset, glm::vec3(0.0f));
        if (props.isOrthographic) {
            d.Read("width", props.orthographic.width, 500.0f);
            d.Read("height", props.orthographic.height, 500.0f);
            d.Read("nearClip", props.orthographic.nearClip, -1.0f);
            d.Read("farClip", props.orthographic.farClip, 1.0f);
        } else {
            d.Read("fieldOfView", props.perspective.fieldOfView, 58.0f);
            d.Read("aspectRatio", props.perspective.aspectRatio, 1.333f);
            d.Read("nearClip", props.perspective.nearClip, 0.1f);
            d.Read("farClip", props.perspective.farClip, 500.0f);
        }
        return new CameraComponent(o, props);
    });

    // ── CameraController3D ────────────────────────────────────────────────────
    ComponentFactory::Register("CameraController3D", [](GameObject* o, Deserializer& d) -> Component* {
        float moveSpeed = 20.0f, lookSpeed = 1.5f, slowMultiplier = 0.2f, fastMultiplier = 5.0f;
        d.Read("moveSpeed", moveSpeed, 20.0f);
        d.Read("lookSpeed", lookSpeed, 1.5f);
        d.Read("slowMultiplier", slowMultiplier, 0.2f);
        d.Read("fastMultiplier", fastMultiplier, 5.0f);
        return new CameraController3D(o, moveSpeed, lookSpeed, slowMultiplier, fastMultiplier);
    });

    // ── LightComponent ────────────────────────────────────────────────────────
    ComponentFactory::Register("LightComponent", [](GameObject* o, Deserializer& d) -> Component* {
        LightProps props;
        props.type = LightType::Directional;
        props.ambient = glm::vec3(0.1f);
        props.diffuse = glm::vec3(1.0f);
        props.specular = glm::vec3(1.0f);
        props.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        props.attenuation = glm::vec3(1.0f, 0.0f, 0.0f);
        props.intensity = 1.0f;
        props.castShadow = false;

        std::string lightTypeStr;
        d.Read("lightType", lightTypeStr, std::string("Directional"));
        d.Read("ambient", props.ambient, glm::vec3(0.1f));
        d.Read("diffuse", props.diffuse, glm::vec3(1.0f));
        d.Read("specular", props.specular, glm::vec3(1.0f));
        d.Read("direction", props.direction, glm::vec3(0.0f, -1.0f, 0.0f));
        d.Read("attenuation", props.attenuation, glm::vec3(1.0f, 0.0f, 0.0f));
        d.Read("intensity", props.intensity, 1.0f);
        d.Read("castShadow", props.castShadow, false);

        if (lightTypeStr == "Point")
            props.type = LightType::Point;
        else if (lightTypeStr == "Spot")
            props.type = LightType::Spot;
        else if (lightTypeStr == "Area")
            props.type = LightType::Area;

        return new LightComponent(o, props);
    });

    // ── VoxelVolumeComponent (micro voxel raymarched volume) ──────────────────
    ComponentFactory::Register("VoxelVolume", [](GameObject* o, Deserializer& d) -> Component* {
        int seed = 1337;
        d.Read("seed", seed);
        auto* c = new VoxelVolumeComponent(o, static_cast<uint32_t>(seed));
        d.Read("gridDim", c->gridDim);
        d.Read("voxelSize", c->voxelSize);
        return c;
    });

    // ── StreamingTerrainComponent (streamed open-world terrain) ────────────────
    // Declares the *scalar* props of a streaming terrain. The height /
    // entity-scatter / splat callbacks are code-only (std::function): a
    // JSON-declared terrain uses the built-in OpenSimplex2 FBm height source
    // (parameterized by "noise") and has no entity scatter. For a custom
    // generator or vegetation, grab the component after load and set the
    // callbacks in C++.
    ComponentFactory::Register("StreamingTerrain", [](GameObject* o, Deserializer& d) -> Component* {
        StreamingTerrainProps p;
        d.Read("worldSize", p.worldSize, p.worldSize);
        d.Read("tileSize", p.tileSize, p.tileSize);
        d.Read("heightScale", p.heightScale, p.heightScale);
        d.Read("tileHeightRes", p.tileHeightRes, p.tileHeightRes);
        d.Read("tileMeshRes", p.tileMeshRes, p.tileMeshRes);
        d.Read("lodCount", p.lodCount, p.lodCount);
        d.Read("lod0RadiusTiles", p.lod0RadiusTiles, p.lod0RadiusTiles);
        d.Read("skirtDepth", p.skirtDepth, p.skirtDepth);
        d.Read("uploadsPerFrame", p.uploadsPerFrame, p.uploadsPerFrame);
        d.Read("tessellationFactor", p.tessellationFactor, p.tessellationFactor);
        d.Read("paletteIndex", p.paletteIndex, p.paletteIndex);
        d.Read("fogDensity", p.fogDensity, p.fogDensity);
        d.Read("fogColor", p.fogColor, p.fogColor);
        d.Read("cacheDir", p.cacheDir, p.cacheDir);
        int cacheVersion = 0;
        d.Read("cacheVersion", cacheVersion, 0);
        p.cacheVersion = static_cast<uint32_t>(cacheVersion);
        d.Read("splatRes", p.splatRes, p.splatRes);
        d.Read("colliderRadiusTiles", p.colliderRadiusTiles, p.colliderRadiusTiles);
        d.Read("colliderResolution", p.colliderResolution, p.colliderResolution);
        d.Read("entityRadiusTiles", p.entityRadiusTiles, p.entityRadiusTiles);
        d.Read("entityTilesPerFrame", p.entityTilesPerFrame, p.entityTilesPerFrame);

        if (auto noise = d.ReadObject("noise")) {
            noise->Read("seed", p.noise.seed, p.noise.seed);
            noise->Read("frequency", p.noise.frequency, p.noise.frequency);
            noise->Read("octaves", p.noise.octaves, p.noise.octaves);
            noise->Read("lacunarity", p.noise.lacunarity, p.noise.lacunarity);
            noise->Read("gain", p.noise.gain, p.noise.gain);
        }

        // Optional shared detail layers: [{ "albedo": "...", "normal": "...",
        // "tiling": 32 }]. Splat weights still come from a code-side splatFn (or
        // the automatic slope/height fallback when none is set).
        for (auto& layer : d.ReadArray("layers")) {
            TerrainLayerDesc desc;
            layer->Read("albedo", desc.albedoPath, std::string());
            layer->Read("normal", desc.normalPath, std::string());
            layer->Read("tiling", desc.tiling, desc.tiling);
            p.layers.push_back(desc);
        }

        return new StreamingTerrainComponent(o, p);
    });

    // ── VoxelWorldComponent (streaming voxel terrain) ────────────────────────
    ComponentFactory::Register("VoxelWorld", [](GameObject* o, Deserializer& d) -> Component* {
        int seed = 42;
        d.Read("seed", seed);
        return new VoxelWorldComponent(o, seed);
    });

    // ── ShapeRendererComponent ────────────────────────────────────────────────
    ComponentFactory::Register("ShapeRendererComponent", [](GameObject* o, Deserializer& d) -> Component* {
        ShapeRendererProps props;
        d.Read("color", props.color, glm::vec4(1.0f));
        d.Read("thickness", props.thickness, 1.0f);
        d.Read("filled", props.filled, false);
        d.Read("radius", props.radius, 10.0f);
        d.Read("boxHalfSize", props.boxHalfSize, glm::vec2(10.0f));
        std::string layerStr;
        d.Read("layer", layerStr, std::string("LAYER_WORLD_2D"));
        props.layer = ParseCanvasLayer(layerStr, CanvasLayer::LAYER_WORLD_2D);
        return new ShapeRendererComponent(o, props);
    });

    // ── Animator2D ────────────────────────────────────────────────────────────
    // Clip data is read via ReadArray so it lives entirely inside this lambda.
    ComponentFactory::Register("Animator2D", [](GameObject* o, Deserializer& d) -> Component* {
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
            if (!clip.name.empty()) animator->AddAnimation(clip.name, clip);
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
    ComponentFactory::Register("ActionManager", [](GameObject* o, Deserializer& d) -> Component* {
        auto* mgr = new ActionManager(o);
        // ParseAction expects a raw nlohmann::json, so we reach through the
        // Deserializer only to enumerate the top-level "actions" array.
        // Each element is reconstructed as a JSONDeserializer-owned value.
        // Since ParseAction is a file-local static function declared later in
        // this translation unit, ActionManager registration is wired up in
        // Application::RegisterActionManager() called from RegisterComponents.
        // See below for the ActionManager post-init hook.
        (void)d;// actions wired up in ParseEntity fallback for now
        return mgr;
    });
}

Application::~Application() {
    if (s_instance == this) s_instance = nullptr;
    ENGINE_LOG("Exiting...");
    if (_window) _window->DeinitImGui();

    _entities.clear();

    for (const auto& layer : _layers) {
        layer->OnDetach();
    }
    _layers.clear();
}

void Application::DestroyEntities() {
    _entities.clear();
}

void Application::Run() {
#ifdef TRACY_ENABLE
    TracyNoop;
    tracy::InitCallstack();
#endif
    _console->Init(this);
    _input->Init(this);
    _audio->Init(this);
    _recorder->setAudioManager(_audio.get());
    if (!_config.headless) {
        _graphics->Init(this);// glad + GL device setup — needs the window's GL context
    }
    _physics->Init(this);// Note that physics debug drawer is dependent on graphics server
    _physics2D->Init(this);
    for (auto& subsystem : _subsystems) {
        subsystem->Init(this);
    }
    ENGINE_LOG("Subsystems initialized.");

    if (_config.headless) {
        // Headless apps may build their world procedurally in OnInit without a
        // scene file; don't leave Update() gated on a GoScene that never comes.
        // (GoScene still works — on native it completes synchronously and
        // re-sets this flag itself.)
        _sceneReady = true;
        OnInit();
        HeadlessLoop();
        return;
    }

    auto windowSize = _window->GetLogicalSize();
    RmlUiManager::Get()->Initialize(windowSize.width, windowSize.height, _graphics->renderer.get());

    _transition.Init();

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

// Fixed-tick Update()-only loop for headless (server) builds: no window, no
// GL, no Render(). Ticks at _config.fixedTimeStep using a monotonic clock and
// sleeps between ticks, so a dedicated server idles instead of spinning a
// core. Exits when Quit() clears _headlessRunning.
void Application::HeadlessLoop() {
    ENGINE_LOG("Headless main loop started (tick = {:.4f} s)", _config.fixedTimeStep);
    const auto tick = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(_config.fixedTimeStep)
    );
    const auto start = std::chrono::steady_clock::now();
    auto nextWake = start + tick;
    float lastTime = 0.0f;

    _headlessRunning = true;
    while (_headlessRunning) {
        const float now = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        _headlessTime = now;// GetWindowTime() source while there is no window
        FrameData frame = { GetClock(), now, now - lastTime };
        lastTime = now;

        Update(frame);
        _clock++;

        std::this_thread::sleep_until(nextWake);
        nextWake += tick;
    }
    ENGINE_LOG("Headless main loop exited.");
}

void Application::PushLayer(Layer* layer) {
    _layers.push_back(std::unique_ptr<Layer>(layer));
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

static std::unique_ptr<Action> ParseAction(const nlohmann::json& val) {
    if (!val.is_object()) return nullptr;

    std::string type = val.value("type", "");
    float duration = val.value("duration", 0.0f);
    std::string easingStr = val.value("easing", "Linear");
    EasingType easing = ParseEasingType(easingStr);

    if (type == "MoveTo") {
        glm::vec3 pos = ParseVec3(val.value("position", nlohmann::json::array()), glm::vec3(0.0f));
        auto action = std::make_unique<MoveTo>(duration, pos);
        action->SetEasing(easing);
        return action;
    } else if (type == "MoveBy") {
        glm::vec3 delta = ParseVec3(val.value("deltaPosition", nlohmann::json::array()), glm::vec3(0.0f));
        auto action = std::make_unique<MoveBy>(duration, delta);
        action->SetEasing(easing);
        return action;
    } else if (type == "RotateTo") {
        glm::vec3 rot(0.0f);
        if (val.contains("rotation") && val["rotation"].is_array() && !val["rotation"].empty()) {
            rot.x = glm::radians(val["rotation"][0].get<float>());
            rot.y = val["rotation"].size() >= 2 ? glm::radians(val["rotation"][1].get<float>()) : 0.0f;
            rot.z = val["rotation"].size() >= 3 ? glm::radians(val["rotation"][2].get<float>()) : 0.0f;
        }
        auto action = std::make_unique<RotateTo>(duration, rot);
        action->SetEasing(easing);
        return action;
    } else if (type == "RotateBy") {
        glm::vec3 delta(0.0f);
        if (val.contains("deltaRotation") && val["deltaRotation"].is_array() && !val["deltaRotation"].empty()) {
            delta.x = glm::radians(val["deltaRotation"][0].get<float>());
            delta.y = val["deltaRotation"].size() >= 2 ? glm::radians(val["deltaRotation"][1].get<float>()) : 0.0f;
            delta.z = val["deltaRotation"].size() >= 3 ? glm::radians(val["deltaRotation"][2].get<float>()) : 0.0f;
        }
        auto action = std::make_unique<RotateBy>(duration, delta);
        action->SetEasing(easing);
        return action;
    } else if (type == "ScaleTo") {
        glm::vec3 scale = ParseVec3(val.value("scale", nlohmann::json::array()), glm::vec3(1.0f));
        auto action = std::make_unique<ScaleTo>(duration, scale);
        action->SetEasing(easing);
        return action;
    } else if (type == "ColorTo") {
        glm::vec4 color = ParseVec4(val.value("color", nlohmann::json::array()), glm::vec4(1.0f));
        auto action = std::make_unique<ColorTo>(duration, color);
        action->SetEasing(easing);
        return action;
    } else if (type == "FadeTo") {
        float alpha = val.value("alpha", 1.0f);
        auto action = std::make_unique<FadeTo>(duration, alpha);
        action->SetEasing(easing);
        return action;
    } else if (type == "Sequence") {
        std::vector<FiniteTimeAction*> seqActions;
        if (val.contains("actions") && val["actions"].is_array()) {
            for (const auto& actVal : val["actions"]) {
                auto parsed = ParseAction(actVal);
                if (parsed) {
                    if (auto* fta = dynamic_cast<FiniteTimeAction*>(parsed.get())) {
                        parsed.release();// Sequence takes ownership below
                        seqActions.push_back(fta);
                    } else {
                        spdlog::warn("ParseAction: Sequence only supports FiniteTimeActions. Action ignored.");
                    }
                }
            }
        }
        if (!seqActions.empty()) {
            return std::make_unique<Sequence>(seqActions);
        }
    } else if (type == "RepeatForever") {
        if (val.contains("action")) {
            auto parsed = ParseAction(val["action"]);
            if (parsed) {
                if (auto* interval = dynamic_cast<ActionInterval*>(parsed.get())) {
                    parsed.release();// RepeatForever takes ownership
                    return std::make_unique<RepeatForever>(interval);
                }
                spdlog::warn("ParseAction: RepeatForever only supports ActionIntervals. Action ignored.");
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
                            if (auto a = ParseAction(actionVal)) mgr->RunAction(std::move(a));
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

// Phase 1: pure JSON parse — no GPU calls, no side effects, can run off-thread.
// Returns a SceneBlueprint with name="" on parse failure. On failure, when
// `error` is non-null it receives the parser's message (for editor reporting).
static SceneBlueprint ParseSceneBlueprint(const std::string& jsonContent, std::string* error = nullptr) {
    SceneBlueprint bp;
    bp.rawJson = jsonContent;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonContent);
    } catch (const std::exception& e) {
        ConsoleSubsystem::Get()->Error(fmt::format("ParseSceneBlueprint: JSON parse error: {}", e.what()));
        if (error) *error = e.what();
        return bp;// bp.name is "" — caller checks this
    }

    bp.name = j.value("name", "Unnamed");

    if (j.contains("textures") && j["textures"].is_array()) {
        for (const auto& tex : j["textures"])
            bp.textures.push_back(tex.get<std::string>());
    }

    if (j.contains("shaders") && j["shaders"].is_object()) {
        for (const auto& [name, shaderVal] : j["shaders"].items()) {
            ShaderProgramProps props;
            props.vert = shaderVal.value("vert", "");
            props.frag = shaderVal.value("frag", "");
            if (shaderVal.contains("tesc")) props.tesc = shaderVal["tesc"].get<std::string>();
            if (shaderVal.contains("tese")) props.tese = shaderVal["tese"].get<std::string>();
            bp.shaders[name] = props;
        }
    }

    if (j.contains("materials") && j["materials"].is_object()) {
        for (const auto& [name, matVal] : j["materials"].items()) {
            MaterialBlueprint mb;
            mb.name = name;
            mb.diffuse = ParseVec3(matVal.value("diffuse", nlohmann::json::array()), mb.diffuse);
            mb.specular = ParseVec3(matVal.value("specular", nlohmann::json::array()), mb.specular);
            mb.ambient = ParseVec3(matVal.value("ambient", nlohmann::json::array()), mb.ambient);
            mb.shininess = matVal.value("shininess", mb.shininess);
            mb.cullFaceEnabled = matVal.value("cullFaceEnabled", mb.cullFaceEnabled);
            mb.baseMap = matVal.value("baseMap", std::string(""));
            mb.normalMap = matVal.value("normalMap", std::string(""));
            mb.aoMap = matVal.value("aoMap", std::string(""));
            mb.roughnessMap = matVal.value("roughnessMap", std::string(""));
            mb.metallicMap = matVal.value("metallicMap", std::string(""));
            mb.heightMap = matVal.value("heightMap", std::string(""));
            bp.materials.push_back(std::move(mb));
        }
    }

    if (j.contains("meshes") && j["meshes"].is_array()) {
        for (const auto& mesh : j["meshes"])
            bp.meshes.push_back(mesh.get<std::string>());
    }

    if (j.contains("entities") && j["entities"].is_array()) {
        for (const auto& entityVal : j["entities"]) {
            EntityBlueprint eb;
            eb.resolvedData = entityVal;
            bp.entities.push_back(std::move(eb));
        }
    }

    return bp;
}

// Phase 2a: upload GPU resources — must run on the main thread.
void Application::LoadSceneResources(const SceneBlueprint& bp) {
    AssetManager::Get().StoreSceneJson(bp.name, bp.rawJson);

    // Default PBR textures are loaded per scene whenever the app opts into them,
    // independently of whether this scene declares any textures of its own —
    // materials and meshes rely on the defaults even in texture-less scenes
    // (e.g. HelloWorld's cube).  There is no startup load point for these, unlike
    // default shaders (loaded once by GraphicsSubsystem).
    if (_config.useDefaultTextures) AssetManager::Get().LoadDefaultTextures();

    std::vector<std::string> texturesToLoad;
    for (const auto& path : bp.textures) {
        if (AssetManager::Get().GetTexture(path) == 0) texturesToLoad.push_back(path);
    }
    if (!texturesToLoad.empty()) {
        AssetManager::Get().LoadTextures(texturesToLoad);
        ENGINE_LOG("JSON Textures created.");
    }

    std::unordered_map<std::string, ShaderProgramProps> shadersToLoad;
    for (const auto& [name, props] : bp.shaders) {
        if (AssetManager::Get().GetShader(name) != nullptr) continue;
        shadersToLoad[name] = props;
    }
    if (!shadersToLoad.empty()) {
        if (_config.useDefaultShaders) AssetManager::Get().LoadDefaultShaders();
        AssetManager::Get().LoadShaders(shadersToLoad);
        ENGINE_LOG("JSON Shaders created.");
    }

    // Materials are created after textures so their map paths resolve to
    // already-loaded TextureHandles. CreateMaterial dedups by name.
    for (const auto& mb : bp.materials) {
        MaterialProps props;
        props.diffuse = mb.diffuse;
        props.specular = mb.specular;
        props.ambient = mb.ambient;
        props.shininess = mb.shininess;
        props.cullFaceEnabled = mb.cullFaceEnabled;
        if (!mb.baseMap.empty()) props.baseMap = TextureHandle(mb.baseMap);
        if (!mb.normalMap.empty()) props.normalMap = TextureHandle(mb.normalMap);
        if (!mb.aoMap.empty()) props.aoMap = TextureHandle(mb.aoMap);
        if (!mb.roughnessMap.empty()) props.roughnessMap = TextureHandle(mb.roughnessMap);
        if (!mb.metallicMap.empty()) props.metallicMap = TextureHandle(mb.metallicMap);
        if (!mb.heightMap.empty()) props.heightMap = TextureHandle(mb.heightMap);
        AssetManager::Get().CreateMaterial(mb.name, props);
    }
    if (!bp.materials.empty()) ENGINE_LOG("JSON Materials created.");

    // TODO: load meshes from bp.meshes
}

// Phase 2b: create GameObjects + Components from resolved blueprints.
void Application::InstantiateScene(const SceneBlueprint& bp) {
    ENGINE_LOG("Loading scene '{}' from JSON...", bp.name);

    auto* container = CreateGameObject();
    container->SetName(bp.name);
    container->parent = GetDefaultGameObject();

    for (const auto& eb : bp.entities)
        ParseEntity(this, eb.resolvedData, container);

    if (!bp.entities.empty()) ENGINE_LOG("JSON Game objects created.");

    mainCamera = _graphics->GetMainCamera();
    mainLight = _graphics->GetMainLight();
}

void Application::ShowLoadingScreen() {
    _transition.Begin();
}

void Application::HideLoadingScreen() {
    _transition.End();
}

// Collect every file that must be in the FileSystem cache before the blueprint's
// resources can be uploaded on the main thread: the scene textures (redirected to
// their .ktx2 variants on WebGL), the default PBR textures (when the app uses
// them), and every shader stage source.  Mirrors the redirect logic in
// AssetManager::LoadTextures so the prefetched path and the loaded path match.
static std::vector<std::string> CollectPrefetchPaths(const SceneBlueprint& bp, bool useDefaultTextures) {
    std::vector<std::string> paths;

#if defined(AE_USE_BASIS_UNIVERSAL) && defined(__EMSCRIPTEN__)
    for (const auto& path : bp.textures) {
        std::string p = (path.size() >= 2 && path[0] == '.' && path[1] == '/') ? path.substr(2) : path;
        if (p.find("aim.png") != std::string::npos || p.find("heightmap") != std::string::npos) {
            paths.push_back(p);
            continue;
        }
        size_t extPos = p.find_last_of('.');
        if (extPos != std::string::npos) {
            std::string ext = p.substr(extPos);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                paths.push_back(p.substr(0, extPos) + ".ktx2");
                continue;
            }
        }
        paths.push_back(p);
    }
    if (useDefaultTextures) {
        paths.push_back("assets/textures/default_diff.ktx2");
        paths.push_back("assets/textures/default_norm.ktx2");
        paths.push_back("assets/textures/default_ao.ktx2");
        paths.push_back("assets/textures/default_rough.ktx2");
        paths.push_back("assets/textures/default_metallic.ktx2");
    }
#else
    paths.insert(paths.end(), bp.textures.begin(), bp.textures.end());
    if (useDefaultTextures) {
        paths.push_back("assets/textures/default_diff.jpg");
        paths.push_back("assets/textures/default_norm.jpg");
        paths.push_back("assets/textures/default_ao.jpg");
        paths.push_back("assets/textures/default_rough.jpg");
        paths.push_back("assets/textures/default_metallic.jpg");
    }
#endif

    for (const auto& [name, props] : bp.shaders) {
        if (!props.vert.empty()) paths.push_back(props.vert);
        if (!props.frag.empty()) paths.push_back(props.frag);
        if (props.tesc.has_value() && !props.tesc->empty()) paths.push_back(*props.tesc);
        if (props.tese.has_value() && !props.tese->empty()) paths.push_back(*props.tese);
    }

    return paths;
}

// Async transition to a scene, by name:
//   show loading screen → prefetch scene file → parse → prefetch assets →
//   unload current scene → load new scene → run onReady, hide loading screen.
// On native the prefetches are synchronous; on WASM they resolve via the browser
// event loop, so the load completes after this returns.
void Application::GoScene(const std::string& sceneName, std::function<void()> onReady) {
    _sceneReady = false;
    ShowLoadingScreen();

    // Runs the caller's hook, marks the scene ready, and fades the overlay out.
    // Called on both success and error so the loading screen never hangs.
    auto finish = [this, onReady]() {
        if (onReady) onReady();
        _sceneReady = true;
        HideLoadingScreen();
    };

    const std::string scenePath = "assets/scenes/" + sceneName + ".json";

    // Stage 1: ensure the scene file itself is cached (async on WASM).
    FileSystem::Get().Prefetch({ scenePath }, [this, sceneName, scenePath, finish]() {
        auto bytes = FileSystem::Get().ReadSync(scenePath);
        if (bytes.empty()) {
            _console->Error(fmt::format("GoScene: failed to read scene file '{}'", scenePath));
            finish();
            return;
        }

        SceneBlueprint bp = ParseSceneBlueprint(std::string(bytes.begin(), bytes.end()));
        if (bp.name.empty()) {
            _console->Error(fmt::format("GoScene: scene blueprint parse failed '{}'", scenePath));
            finish();
            return;
        }

        // Stage 2: prefetch every asset the blueprint declares, then load.
        std::vector<std::string> assetPaths = CollectPrefetchPaths(bp, _config.useDefaultTextures);
        _console->Info(fmt::format("GoScene: prefetching {} asset(s) for '{}'", assetPaths.size(), sceneName));

        FileSystem::Get().Prefetch(assetPaths, [this, sceneName, bp = std::move(bp), finish]() mutable {
            // Unload the current scene first. This runs under the loading overlay,
            // so freeing the old scene before loading the new one is invisible.
            UnloadCurrentScene();

            _currentSceneName = sceneName;

            // The single place where scene assets are uploaded and entities are
            // instantiated.
            ENGINE_LOG("GoScene: loading '{}'...", sceneName);
            LoadSceneResources(bp);
            InstantiateScene(bp);

            finish();

            // Drop the raw byte cache — every asset is now a GPU object or a
            // parsed shader; keeping the bytes just bloats the WASM heap.
            FileSystem::Get().ClearCache();
        });
    });
}

void Application::ReloadScene() {
    if (_currentSceneName.empty()) return;
    GoScene(_currentSceneName, [this] { OnLoad(); });
}

void Application::LoadEditorScene(const uint8_t* data, size_t len) {
    // Mirror GoScene's entity-clearing logic without requiring a scene file.
    std::erase_if(_entities, [this](const std::unique_ptr<GameObject>& e) { return e.get() != _defaultGameObject; });
    _nextEntityID = _defaultGameObject ? 1 : 0;

    _graphics->cameras.clear();
    _graphics->directionalLights.clear();
    _graphics->pointLights.clear();

    _audio->StopAll();
    _physics->Reset();

    SceneLoader loader(this);
    auto result = loader.LoadFromBuffer(data, len);
    if (!result.success) {
        _editorSceneError = result.error;
        _console->Warn(fmt::format("[Editor] Scene load failed: {}", result.error));
    } else {
        _editorSceneError.clear();
        _console->Info(fmt::format("[Editor] Scene loaded: {} node(s)", result.allNodes.size()));
    }

    _sceneReady = true;
}

void Application::UnloadScene(const std::string& name) {
    if (!_defaultGameObject) return;

    // Find the scene container (direct child of __root__ with this name).
    GameObject* container = nullptr;
    for (const auto& e : _entities) {
        if (e.get() != _defaultGameObject && e->parent == _defaultGameObject && e->GetName() == name) {
            container = e.get();
            break;
        }
    }
    if (!container) return;

    // Collect the full subtree iteratively (no children list on GameObject).
    std::unordered_set<GameObject*> toRemove;
    std::vector<GameObject*> stack = { container };
    while (!stack.empty()) {
        auto* node = stack.back();
        stack.pop_back();
        toRemove.insert(node);
        for (const auto& e : _entities) {
            if (e->parent == node) stack.push_back(e.get());
        }
    }

    // Erasing destroys the objects; each GameObject detaches its components
    // (unregistering them from the graphics/physics servers) as it dies.
    std::erase_if(_entities, [&toRemove](const std::unique_ptr<GameObject>& e) { return toRemove.contains(e.get()); });

    // Free GPU/CPU assets that were first loaded by this scene.
    // Must come after GameObjects are deleted so no component holds a dangling ref.
    AssetManager::Get().UnloadSceneAssets(name);

    mainCamera = _graphics->GetMainCamera();
    mainLight = _graphics->GetMainLight();
    _console->Info(fmt::format("[Scene] Unloaded scene '{}'.", name));
}

void Application::ClearScenes() {
    if (!_defaultGameObject) return;
    // Collect names of all direct children of __root__ first to avoid
    // iterator invalidation inside UnloadScene.
    std::vector<std::string> names;
    for (const auto& e : _entities) {
        if (e.get() != _defaultGameObject && e->parent == _defaultGameObject) names.push_back(e->GetName());
    }
    for (const auto& n : names)
        UnloadScene(n);
}

void Application::UnloadCurrentScene() {
    ClearScenes();// delete scene GameObjects

    // Deleting the GameObjects does not unregister their components from the
    // graphics render lists (GameObject's destructor doesn't run OnDetach — see
    // the component-lifecycle TODO), so those lists would otherwise keep dangling
    // pointers to freed owners and re-draw the previous scene. Clear every
    // scene-populated list here, mirroring the camera/light reset.
    _graphics->cameras.clear();
    _graphics->directionalLights.clear();
    _graphics->pointLights.clear();
    _graphics->sunComponents.clear();
    _graphics->renderables.clear();// MeshComponent
    _graphics->canvasDrawables.clear();// SpriteComponent / Text2DComponent / ...

    _audio->StopAll();
    _physics->Reset();
    if (!_currentSceneName.empty()) AssetManager::Get().ClearSceneAssets();// free scene GPU assets
    _currentSceneName.clear();
}

void Application::AddScene(const std::string& json) {
    SceneBlueprint bp = ParseSceneBlueprint(json, &_editorSceneError);
    if (bp.name.empty()) {// parse failed — _editorSceneError set by ParseSceneBlueprint
        _lastLoadedScene = "";
        _console->Warn(fmt::format("[Scene] AddScene JSON parse error: {}", _editorSceneError));
        return;
    }

    // Replace any existing scene with the same name.
    UnloadScene(bp.name);

    try {
        LoadSceneResources(bp);
        InstantiateScene(bp);
        _editorSceneError.clear();
        _lastLoadedScene = bp.name;
        _console->Info(fmt::format("[Scene] Added scene '{}'.", bp.name));
    } catch (const std::exception& e) {
        _editorSceneError = e.what();
        _lastLoadedScene = "";
        _console->Warn(fmt::format("[Scene] AddScene '{}' failed: {}", bp.name, e.what()));
    }
}

std::string Application::GetLoadedScenes() const {
    nlohmann::json arr = nlohmann::json::array();
    if (_defaultGameObject) {
        for (const auto& e : _entities) {
            if (e.get() != _defaultGameObject && e->parent == _defaultGameObject) arr.push_back(e->GetName());
        }
    }
    return arr.dump();
}

std::string Application::GetLastLoadedScene() const {
    return _lastLoadedScene;
}

const std::string& Application::GetEditorSceneError() const {
    return _editorSceneError;
}

void Application::Quit() {
    ENGINE_LOG("Requested to quit.");
    if (_window) {
        _window->Close();
    } else {
        _headlessRunning = false;
    }
}

void Application::DeferSpawn(std::function<void()> cmd) {
    _spawnQueue.push_back(std::move(cmd));
}

void Application::Update(const FrameData& props) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Application::Update");
#endif
    float dt = props.deltaTime;

    // Update RmlUI and tick the loading-screen transition regardless of whether
    // the scene is ready, so the overlay animates during transitions.
    RmlUiManager::Get()->Update(dt);
    _transition.Update(dt);

    if (!_sceneReady) return;

    // Flush deferred spawns queued by component OnTick last frame.
    // This runs before OnUpdate and GameLayer::OnUpdate, so _entities is not
    // being iterated and CreateGameObject is safe to call here.
    {
        std::vector<std::function<void()>> queue;
        queue.swap(_spawnQueue);
        for (auto& cmd : queue)
            cmd();
    }

    OnUpdate(dt, GetWindowTime());

    // ecs.Process(dt); // Note that most of the entity manipulation logic should be put there
    _console->Process(dt);
    if (!_config.headless) _input->Process(dt);// polls Window::Get(), which is null headless
    _audio->Process(dt);
    _physics->Process(dt);// TODO: Update only every entity's physics transform
    _physics2D->Process(dt);
    _graphics->Process(dt);
    for (auto& subsystem : _subsystems) {
        subsystem->Process(dt);
    }

#if SHOW_PROCESS_COST
    ENGINE_LOG(fmt::format("Update costs {} ms", (GetWindowTime() - time) * 1000));
#endif

    for (const auto& layer : _layers) {
        layer->OnUpdate(dt);
    }

    float time = GetWindowTime();

    for (const auto& go : _entities) {
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

    for (const auto& layer : _layers) {
        layer->OnRender(dt);
    }

    // Runs on the render (GL) thread, after the frame is drawn — the only safe
    // place to read back pixels for recording / screenshots.
    UpdateAutoCapture();

    // Feed the encoder one frame per render tick whenever recording is active,
    // whether it was started by the auto-capture sequence or the manual F2 key.
    if (_recorder && _recorder->isRecording()) _recorder->captureFrame();

#if SHOW_RENDER_AND_DRAW_COST
    ENGINE_LOG(fmt::format("Render & draw cost {} ms", (GetWindowTime() - time) * 1000));
#endif
}

void Application::UpdateAutoCapture() {
    if (_capState == CaptureState::Idle || _capState == CaptureState::Done) return;

    float now = GetWindowTime();
    if (_capPhaseStart < 0.0f) _capPhaseStart = now;// lazy init: anchor to the first rendered frame
    float elapsed = now - _capPhaseStart;

    Renderer* renderer = _graphics->renderer.get();

    auto ensureParentDir = [](const std::string& path) {
        std::error_code ec;
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    };

    // Builds "name_000.png" style paths for a screenshot burst, or the bare
    // output path when only a single shot is requested (duration <= 0).
    auto screenshotPath = [this](int index) -> std::string {
        if (_autoCap.duration <= 0.0f) return _autoCap.outputPath;
        std::filesystem::path p(_autoCap.outputPath);
        std::string ext = p.extension().string();
        if (ext.empty()) ext = ".png";
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
            _nextShotTime = 0.0f;
            _capState = CaptureState::Capturing;
        }
        break;

    case CaptureState::Capturing: {
        // Video frames are pumped by Render() via captureFrame(); here we only
        // drive the screenshot cadence and the overall capture-window timing.
        if (_autoCap.mode == AutoCaptureConfig::Mode::Screenshot && elapsed >= _nextShotTime) {
            SaveScreenshot(screenshotPath(_screenshotIndex));
            ++_screenshotIndex;
            _nextShotTime += 1.0f;// one shot per second across the window
        }

        bool singleShotDone =
            _autoCap.mode == AutoCaptureConfig::Mode::Screenshot && _autoCap.duration <= 0.0f && _screenshotIndex >= 1;
        if (singleShotDone || elapsed >= _autoCap.duration) _capState = CaptureState::Finishing;
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
    if (!_graphics->renderer) return;
    _graphics->renderer->readPixelsAsync([&path](const GpuImageData& img) {
        if (img.data.empty()) return;
        bool ok = PngWrite(path, img.data.data(), img.width, img.height, img.channelCount);
        FrameStats st = AnalyzeFrame(img.data.data(), img.width, img.height, img.channelCount);
        // Machine-readable marker consumed by scripts/smokeTest.sh (grepped from
        // stdout). Flushed so it survives the imminent auto-quit.
        fmt::print(
            "[Smoke] result path={} size={}x{} meanRGB={:.1f},{:.1f},{:.1f} spread={} blank={} write={}\n",
            path,
            img.width,
            img.height,
            st.meanR,
            st.meanG,
            st.meanB,
            st.spread,
            st.blank ? 1 : 0,
            ok ? 1 : 0
        );
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

float Application::GetWindowTime() {
    if (!_window) return _headlessTime;
    return this->_window->GetTime();
}

void Application::SetWindowTime(float time) {
    if (!_window) {
        _headlessTime = time;
        return;
    }
    this->_window->SetTime(time);
}

std::string Application::GetWindowTitle() {
    if (!_window) return _config.windowTitle;
    return this->_window->GetTitle();
}

void Application::SetWindowTitle(const std::string& title) {
    if (!_window) return;
    this->_window->SetTitle(title);
}

GameObject* Application::CreateGameObject(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
    auto owned = std::make_unique<GameObject>(this, position, rotation, scale);
    auto* e = owned.get();
    e->SetName(fmt::format("entity #{}", _nextEntityID++));
    _entities.push_back(std::move(owned));
    return e;
}

GameObject* Application::CreateGameObject(glm::vec2 position, float angle) {
    auto owned = std::make_unique<GameObject>(
        this, glm::vec3(position.x, position.y, 0.0f), glm::vec3(0.0f, 0.0f, angle), glm::vec3(1.0f)
    );
    auto* e = owned.get();
    e->SetName(fmt::format("entity #{}", _nextEntityID++));
    _entities.push_back(std::move(owned));
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
        // clang-format off
        // NOLINTBEGIN
        int jsHeapBytes = EM_ASM_INT({
            if (globalThis.performance && globalThis.performance.memory) {
                return globalThis.performance.memory.usedJSHeapSize;
            }
            return -1;
        });
        // NOLINTEND
        // clang-format on
        if (jsHeapBytes >= 0) {
            jsHeapSizeMB = static_cast<double>(jsHeapBytes) / mb;
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

}// extern "C"
#endif

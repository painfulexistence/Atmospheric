#pragma once
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#define GLES_SILENCE_DEPRECATION
#endif
#if defined(__EMSCRIPTEN__) || defined(ANDROID)
#include <GLES3/gl3.h>
#ifndef IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_ES3
#endif
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <OpenGLES/ES3/gl.h>
#ifndef IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_ES3
#endif
#else
#include <glad/glad.h>
#endif
#else
#include <glad/glad.h>
#endif

// #include <pch.hpp>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#ifndef TRACY_ENABLE
#define ZoneScoped
#define ZoneScopedN(name)
#define FrameMark
#define TracyNoop
#endif

#define CAMERA_ANGULAR_OFFSET 0.05
#define CAMERA_SPEED 15
#define CAMERA_VERTICAL_SPEED 8
#define PI 3.1416
#define GRAVITY 9.8
#define FIXED_TIME_STEP 1.0 / 60.0

struct TextureHandle {
    static constexpr uint32_t INVALID = 0;
    uint32_t id = INVALID;

    TextureHandle() = default;
    TextureHandle(uint32_t val) : id(val) {}
    TextureHandle(int val) : id(val == -1 ? INVALID : static_cast<uint32_t>(val)) {}
    TextureHandle(const char* path);
    TextureHandle(const std::string& path);

    bool IsValid() const { return id != INVALID; }
    bool operator==(const TextureHandle& other) const { return id == other.id; }
    bool operator!=(const TextureHandle& other) const { return id != other.id; }
    bool operator==(uint32_t val) const { return id == val; }
    bool operator!=(uint32_t val) const { return id != val; }
    bool operator==(int val) const { return (val == -1 ? id == INVALID : id == static_cast<uint32_t>(val)); }
    bool operator!=(int val) const { return !(*this == val); }

    // Implicit conversion to uint32_t/int to return raw GL texture ID directly
    operator uint32_t() const { return id; }
    operator int() const { return static_cast<int>(id); }
    operator bool() const noexcept { return IsValid(); }
};

struct ShaderHandle {
    static constexpr uint32_t INVALID = 0;
    uint32_t id = INVALID;

    ShaderHandle() = default;
    ShaderHandle(uint32_t val) : id(val) {}
    ShaderHandle(int val) : id(val == -1 ? INVALID : static_cast<uint32_t>(val)) {}

    bool IsValid() const { return id != INVALID; }
    bool operator==(const ShaderHandle& other) const { return id == other.id; }
    bool operator!=(const ShaderHandle& other) const { return id != other.id; }
    bool operator==(uint32_t val) const { return id == val; }
    bool operator!=(uint32_t val) const { return id != val; }
    bool operator==(int val) const { return (val == -1 ? id == INVALID : id == static_cast<uint32_t>(val)); }
    bool operator!=(int val) const { return !(*this == val); }

    operator uint32_t() const { return id; }
    operator int() const { return static_cast<int>(id); }
    operator bool() const noexcept { return IsValid(); }
};

struct MaterialHandle {
    static constexpr uint32_t INVALID = 0;
    uint32_t id = INVALID;

    MaterialHandle() = default;
    MaterialHandle(uint32_t val) : id(val) {}
    MaterialHandle(int val) : id(val == -1 ? INVALID : static_cast<uint32_t>(val)) {}

    bool IsValid() const { return id != INVALID; }
    bool operator==(const MaterialHandle& other) const { return id == other.id; }
    bool operator!=(const MaterialHandle& other) const { return id != other.id; }
    bool operator==(uint32_t val) const { return id == val; }
    bool operator!=(uint32_t val) const { return id != val; }
    bool operator==(int val) const { return (val == -1 ? id == INVALID : id == static_cast<uint32_t>(val)); }
    bool operator!=(int val) const { return !(*this == val); }

    operator uint32_t() const { return id; }
    operator int() const { return static_cast<int>(id); }
    operator bool() const noexcept { return IsValid(); }
};

struct FontHandle {
    static constexpr uint32_t INVALID = 0;
    uint32_t id = INVALID;

    FontHandle() = default;
    FontHandle(uint32_t val) : id(val) {}
    FontHandle(int val) : id(val == -1 ? INVALID : static_cast<uint32_t>(val)) {}

    bool IsValid() const { return id != INVALID; }
    bool operator==(const FontHandle& other) const { return id == other.id; }
    bool operator!=(const FontHandle& other) const { return id != other.id; }
    bool operator==(uint32_t val) const { return id == val; }
    bool operator!=(uint32_t val) const { return id != val; }
    bool operator==(int val) const { return (val == -1 ? id == INVALID : id == static_cast<uint32_t>(val)); }
    bool operator!=(int val) const { return !(*this == val); }

    operator uint32_t() const { return id; }
    operator int() const { return static_cast<int>(id); }
    operator bool() const noexcept { return IsValid(); }
};

struct MeshHandle {
    static constexpr uint32_t INVALID = 0;
    uint32_t id = INVALID;

    MeshHandle() = default;
    MeshHandle(uint32_t val) : id(val) {}
    MeshHandle(int val) : id(val == -1 ? INVALID : static_cast<uint32_t>(val)) {}

    bool IsValid() const { return id != INVALID; }
    bool operator==(const MeshHandle& other) const { return id == other.id; }
    bool operator!=(const MeshHandle& other) const { return id != other.id; }
    bool operator==(uint32_t val) const { return id == val; }
    bool operator!=(uint32_t val) const { return id != val; }
    bool operator==(int val) const { return (val == -1 ? id == INVALID : id == static_cast<uint32_t>(val)); }
    bool operator!=(int val) const { return !(*this == val); }

    operator uint32_t() const { return id; }
    operator int() const { return static_cast<int>(id); }
    operator bool() const noexcept { return IsValid(); }
};

enum Axis { UP, DOWN, BACK, FRONT, RIGHT, LEFT };

// Canvas layer constants for z-ordering
// World 3D layers (< LAYER_WORLD_2D): rendered by WorldCanvasPass with depth testing
// World 2D layers (LAYER_WORLD_2D): rendered by CanvasPass, screen-space ortho, no depth
// UI layers (>= LAYER_UI_BACK): rendered by UIPass (RmlUi)
enum class CanvasLayer {
    // 3D world layers (WorldCanvasPass - depth tested)
    LAYER_BACKGROUND = 0,// Far background (parallax, sky)
    LAYER_WORLD_BACK = 10,// Background game objects
    LAYER_WORLD = 50,// Main game objects (player, enemies)
    LAYER_WORLD_FRONT = 90,// Foreground game objects
    LAYER_EFFECTS = 100,// Particle effects, damage numbers
    // 2D layer (CanvasPass - screen space, no depth)
    LAYER_WORLD_2D = 150,// Pure 2D sprites (screen coordinates)
    // UI layers (UIPass - RmlUi)
    LAYER_UI_BACK = 200,// UI background elements
    LAYER_UI = 300,// Main UI elements (HUD, health bars)
    LAYER_UI_FRONT = 400,// Popups, tooltips
    LAYER_OVERLAY = 500,// Debug overlay, screen fade
};

// Billboard modes for 3D sprites
enum class BillboardMode {
    None,// No billboarding, use object's rotation
    ViewPoint,// Face camera position (spherical)
    ViewPlane,// Face camera plane (cylindrical, Y-axis locked)
};

enum class ShapeType {
    Cube,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Custom,
};

struct CubeShapeData {
    glm::vec3 size;
};

struct SphereShapeData {
    float radius;
};

struct CapsuleShapeData {
    float radius;
    float height;
};

struct CylinderShapeData {
    float radius;
    float height;
};

struct ConeShapeData {
    float radius;
    float height;
};

struct Shape {
    ShapeType type;
    union {
        CubeShapeData cubeData;
        SphereShapeData sphereData;
        CapsuleShapeData capsuleData;
        CylinderShapeData cylinderData;
        ConeShapeData coneData;
    } data;
};

namespace std {
    template<> struct hash<TextureHandle> {
        size_t operator()(const TextureHandle& h) const noexcept { return hash<uint32_t>{}(h.id); }
    };
    template<> struct hash<ShaderHandle> {
        size_t operator()(const ShaderHandle& h) const noexcept { return hash<uint32_t>{}(h.id); }
    };
    template<> struct hash<MaterialHandle> {
        size_t operator()(const MaterialHandle& h) const noexcept { return hash<uint32_t>{}(h.id); }
    };
    template<> struct hash<FontHandle> {
        size_t operator()(const FontHandle& h) const noexcept { return hash<uint32_t>{}(h.id); }
    };
    template<> struct hash<MeshHandle> {
        size_t operator()(const MeshHandle& h) const noexcept { return hash<uint32_t>{}(h.id); }
    };
}

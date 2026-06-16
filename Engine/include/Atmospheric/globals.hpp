#pragma once
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
// #define GLEW_BUILD // build GLEW as dynamic library
// #include <GL/glew.h>
#include <glad/glad.h>
#endif

// #include <pch.hpp>
#include <fmt/core.h>
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
#pragma once
#include "globals.hpp"
#include <glm/vec3.hpp>

enum class RenderQueue {
    Background = 1000,// Skybox, far background
    Opaque = 2000,// Normal opaque objects
    AlphaTest = 2450,// Objects with alpha testing (vegetation, etc.)
    Transparent = 3000,// Transparent objects (glass, particles, etc.)
    Overlay = 4000// UI, HUD, debug overlays
};

struct MaterialProps {
    int baseMap = -1;
    int normalMap = -1;
    int aoMap = -1;
    int roughnessMap = -1;
    int metallicMap = -1;
    int heightMap = -1;
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);
    glm::vec3 specular = glm::vec3(.7, .7, .7);
    glm::vec3 ambient = glm::vec3(0, 0, 0);
    float shininess = .25;
    bool cullFaceEnabled = true;
    GLenum primitiveType = GL_TRIANGLES;
#ifndef __EMSCRIPTEN__
    GLenum polygonMode = GL_FILL;
#endif
};

class Material {
public:
    int baseMap = -1;
    int normalMap = -1;
    int aoMap = -1;
    int roughnessMap = -1;
    int metallicMap = -1;
    int heightMap = -1;
    glm::vec3 diffuse = glm::vec3(.55, .55, .55);
    glm::vec3 specular = glm::vec3(.7, .7, .7);
    glm::vec3 ambient = glm::vec3(0, 0, 0);
    float shininess = .25;
    bool cullFaceEnabled = true;
    GLenum primitiveType = GL_TRIANGLES;
#ifndef __EMSCRIPTEN__
    GLenum polygonMode = GL_FILL;
#endif

    RenderQueue renderQueue = RenderQueue::Opaque;
    int renderQueueOffset = 0;// Fine-tune rendering order within queue

    int GetFinalRenderQueue() const {
        return static_cast<int>(renderQueue) + renderQueueOffset;
    }

    Material(const MaterialProps& props) {
        baseMap = props.baseMap;
        normalMap = props.normalMap;
        aoMap = props.aoMap;
        roughnessMap = props.roughnessMap;
        metallicMap = props.metallicMap;
        heightMap = props.heightMap;
        diffuse = props.diffuse;
        specular = props.specular;
        ambient = props.ambient;
        shininess = props.shininess;
        cullFaceEnabled = props.cullFaceEnabled;
        primitiveType = props.primitiveType;
#ifndef __EMSCRIPTEN__
        polygonMode = props.polygonMode;
#endif
    }
};
#pragma once
#include "camera_component.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "shader.hpp"

class GameObject;

struct SceneNode {
    std::string name;
    SceneNode* parent;
    std::vector<SceneNode*> children;
    GameObject* gameObject;

    void AddChild(SceneNode* node);

    void RemoveChild(SceneNode* node);
};

class Scene {
public:
    Scene() : root(nullptr) {}

    SceneNode* GetRoot() {
        return root;
    }

private:
    SceneNode* root;
};
#pragma once
#include "camera_component.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "shader.hpp"

class GameObject;

struct GameObjectProps {
    std::string name;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    std::optional<CameraProps> camera;
    std::optional<LightProps> light;
};

struct SceneDef {
    std::vector<std::string> textures;
    std::unordered_map<std::string, ShaderProgramProps> shaders;
    std::vector<MaterialProps> materials;
    std::vector<GameObjectProps> gameObjects;
};

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
    Scene(const SceneDef& data);

    SceneNode* GetRoot() {
        return root;
    }

private:
    SceneNode* root;
};
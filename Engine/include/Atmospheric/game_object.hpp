#pragma once
#include "globals.hpp"
#include <map>

class Application;
class TransformComponent;

class Component;

struct LightProps;
struct CameraProps;
struct RigidbodyProps;
struct SpriteProps;

class Mesh;

class GraphicsServer;

class PhysicsServer;

class GameObject {
public:
    GameObject* parent = nullptr;
    bool isActive = true;

    GameObject(
      Application* app,
      glm::vec3 position = glm::vec3(0.0f),
      glm::vec3 rotation = glm::vec3(0.0f),
      glm::vec3 scale = glm::vec3(1.0f)
    );
    ~GameObject();

    template<typename T> T* GetComponent() const {
        for (auto* component : _components) {
            if (T* cast = dynamic_cast<T*>(component)) {
                return cast;
            }
        }
        return nullptr;
    }
    // Component* GetComponent(std::string name) const;
    template<typename T, typename... Args> Component* AddComponent(Args&&... args) {
        T* component = new T(this, std::forward<Args>(args)...);
        _components.push_back(component);
        component->gameObject = this;
        component->OnAttach();
        return component;
    }
    void AddComponent(Component* component);
    void RemoveComponent(Component* component);

    void Tick(float dt);
    void PhysicsTick(float dt);

    Application* GetApp() const {
        return _app;
    }

    GameObject* AddLight(const LightProps&);
    GameObject* AddCamera(const CameraProps&);
    GameObject* AddMesh(const std::string& meshName);
    GameObject* AddMesh(Mesh* mesh);
    GameObject* AddSprite(const SpriteProps& props);
    GameObject* AddRigidbody(const RigidbodyProps& props);
    // GameObject* AddRigidbody(const std::string& meshName, float mass = 0.0f, glm::vec3 linearFactor =
    // glm::vec3(1.0f), glm::vec3 angularFactor = glm::vec3(1.0f)); GameObject* AddRigidbody(Mesh* mesh, float mass =
    // 0.0f, glm::vec3 linearFactor = glm::vec3(1.0f), glm::vec3 angularFactor = glm::vec3(1.0f));

    glm::mat4 GetLocalTransform() const;
    void SetLocalTransform(glm::mat4 xform);

    glm::mat4 GetObjectTransform() const;
    void SetObjectTransform(glm::mat4 xform);

    void SyncObjectTransform(glm::mat4 xform);

    glm::vec3 GetPosition() const;
    glm::vec3 GetRotation() const;// radians
    glm::vec3 GetScale() const;
    void SetPosition(glm::vec3 value);
    void SetRotation(glm::vec3 value);// radians
    void SetScale(glm::vec3 value);

    // User-friendly rotation API (degrees)
    glm::vec3 GetEulerAngles() const;
    void SetEulerAngles(glm::vec3 degrees);

    glm::mat4 GetTransform() const;// World space

    glm::vec3 GetVelocity();
    void SetVelocity(glm::vec3 value);

    inline void SetActive(bool value) {
        isActive = value;
    }

    void SetPhysicsActivated(bool value);

    inline const std::string& GetName() {
        return _name;
    }
    inline void SetName(const std::string& name) {
        _name = name;
    }

    void SetCollisionCallback(std::function<void(GameObject*)> callback) {
        _collisionCallback = callback;
    }

    void OnCollision(GameObject* other);

private:
    std::string _name = " ";
    std::vector<Component*> _components;
    // std::map<std::string, Component*> _namedComponents;
    Application* _app = nullptr;
    TransformComponent* _transform = nullptr;
    glm::vec3 _velocity = glm::vec3(0, 0, 0);
    glm::vec3 _angularVelocity = glm::vec3(0, 0, 0);
    bool _isTransformDirty = false;
    std::function<void(GameObject*)> _collisionCallback;
};
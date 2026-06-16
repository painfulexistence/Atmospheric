#include "game_object.hpp"
#include "application.hpp"
#include "component.hpp"
#include "graphics_server.hpp"
#include "physics_server.hpp"
#include "rigidbody_component.hpp"
#include "sprite_component.hpp"
#include "transform_component.hpp"

GameObject::GameObject(Application* app, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) : _app(app) {
    _transform = new TransformComponent(this, position, rotation, scale);
    AddComponent(_transform);
}

GameObject::~GameObject() {
    // Components are deleted by the component system
    // _transform will be deleted when _components are cleaned up
}

void GameObject::AddComponent(Component* component) {
    _components.push_back(component);
    // _namedComponents.insert_or_assign(component->GetName(), component);
    component->gameObject = this;
    component->OnAttach();
}

void GameObject::RemoveComponent(Component* component) {
    auto it = std::find(_components.begin(), _components.end(), component);
    if (it != _components.end()) {
        component->OnDetach();
        _components.erase(it);
    }
    // _namedComponents.erase(component->GetName());
}

// Component* GameObject::GetComponent(std::string name) const
// {
//     auto it = _namedComponents.find(name);
//     if (it == _namedComponents.end())
//         return nullptr;
//     else
//         return it->second;
// }

void GameObject::Tick(float dt) {
    if (!isActive) return;
    for (auto* component : _components) {
        if (component->CanTick()) {
            component->OnTick(dt);
        }
    }
}

void GameObject::PhysicsTick(float dt) {
    if (!isActive) return;
    for (auto* component : _components) {
        if (component->CanPhysicsTick()) {
            component->OnPhysicsTick(dt);
        }
    }
}

// Shortcut for adding light component
GameObject* GameObject::AddLight(const LightProps& props) {
    AddComponent<LightComponent>(props);
    return this;
}

// Shortcut for adding camera component
GameObject* GameObject::AddCamera(const CameraProps& props) {
    AddComponent<CameraComponent>(props);
    return this;
}

// Shortcut 1 for adding renderable component
GameObject* GameObject::AddMesh(const std::string& meshName) {
    if (_app) {
        auto graphics = _app->GetGraphicsServer();
        if (graphics) {
            auto mesh = graphics->GetMesh(meshName);
            AddMesh(mesh);
        }
    }
    return this;
}

// Shortcut 2 for adding renderable component
GameObject* GameObject::AddMesh(Mesh* mesh) {
    AddComponent<MeshComponent>(mesh);
    return this;
}

GameObject* GameObject::AddSprite(const SpriteProps& props) {
    AddComponent<SpriteComponent>(props);
    return this;
}

// // Shortcut 1 for adding impostor component
// GameObject* GameObject::AddRigidbody(const std::string& meshName, float mass, glm::vec3 linearFactor, glm::vec3
// angularFactor)
// {
//     if (_graphics && _physics) {
//         auto mesh = _graphics->GetMesh(meshName);
//         auto impostor =  new RigidbodyComponent(this, mesh->GetShape(), mass);
//         impostor->SetLinearFactor(linearFactor);
//         impostor->SetAngularFactor(angularFactor);
//         _physics->AddRigidbody(impostor);
//     }
//     return this;
// }

// // Shortcut 2 for adding impostor component
// GameObject* GameObject::AddRigidbody(Mesh* mesh, float mass, glm::vec3 linearFactor, glm::vec3 angularFactor)
// {
//     if (_physics) {
//         auto impostor =  new RigidbodyComponent(this, mesh->GetShape(), mass);
//         impostor->SetLinearFactor(linearFactor);
//         impostor->SetAngularFactor(angularFactor);
//         _physics->AddRigidbody(impostor);
//     }
//     return this;
// }

GameObject* GameObject::AddRigidbody(const RigidbodyProps& props) {
    AddComponent<RigidbodyComponent>(props);
    return this;
}

glm::mat4 GameObject::GetLocalTransform() const {
    return _transform->GetLocalTransform();
}

void GameObject::SetLocalTransform(glm::mat4 xform) {
    _transform->SetLocalTransform(xform);
}

glm::mat4 GameObject::GetObjectTransform() const {
    return _transform->GetWorldTransform();
}

void GameObject::SetObjectTransform(glm::mat4 xform) {
    _transform->SetWorldTransform(xform);

    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb) rb->SetWorldTransform(_transform->GetPosition(), _transform->GetRotation());
}

void GameObject::SyncObjectTransform(glm::mat4 xform) {
    _transform->SyncWorldTransform(xform);
}

glm::vec3 GameObject::GetPosition() const {
    return _transform->GetPosition();
}

void GameObject::SetPosition(glm::vec3 value) {
    _transform->SetPosition(value);

    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb) rb->SetWorldTransform(_transform->GetPosition(), _transform->GetRotation());
}

glm::vec3 GameObject::GetRotation() const {
    return _transform->GetRotation();
}

void GameObject::SetRotation(glm::vec3 value) {
    _transform->SetRotation(value);

    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb) rb->SetWorldTransform(_transform->GetPosition(), _transform->GetRotation());
}

glm::vec3 GameObject::GetEulerAngles() const {
    return _transform->GetEulerAngles();
}

void GameObject::SetEulerAngles(glm::vec3 degrees) {
    _transform->SetEulerAngles(degrees);

    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb) rb->SetWorldTransform(_transform->GetPosition(), _transform->GetRotation());
}

glm::vec3 GameObject::GetScale() const {
    return _transform->GetScale();
}

void GameObject::SetScale(glm::vec3 value) {
    _transform->SetScale(value);
}

glm::mat4 GameObject::GetTransform() const {
    return _transform->GetWorldTransform();
}

glm::vec3 GameObject::GetVelocity() {
    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb == nullptr) throw std::runtime_error("RigidbodyComponent not found");

    return rb->GetLinearVelocity();
}

void GameObject::SetVelocity(glm::vec3 value) {
    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb == nullptr) throw std::runtime_error("RigidbodyComponent not found");

    rb->SetLinearVelocity(value);
}

void GameObject::SetPhysicsActivated(bool value) {
    RigidbodyComponent* rb = GetComponent<RigidbodyComponent>();
    if (rb == nullptr) return;

    if (value) {
        rb->WakeUp();
    } else {
        rb->Sleep();
    }
}

void GameObject::OnCollision(GameObject* other) {
    if (_collisionCallback) {
        _collisionCallback(other);
    }
}
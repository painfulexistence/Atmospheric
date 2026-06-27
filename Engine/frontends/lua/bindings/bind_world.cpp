#include "../lua_application.hpp"
#include "../scriptable_component.hpp"

void BindWorldAPI(sol::state& lua, LuaApplication* app)
{
    sol::table atmos = lua["atmos"];

    // ===== ScriptableComponent usertype =====
    lua.new_usertype<ScriptableComponent>("ScriptableComponent",
        sol::no_constructor,

        // Access the Lua instance table
        "instance", sol::property(&ScriptableComponent::GetInstance),

        // Get the class name
        "className", sol::property(&ScriptableComponent::GetClassName),

        // Check if a method exists
        "hasMethod", &ScriptableComponent::HasMethod,

        // Access the owning GameObject
        "gameObject", sol::readonly(&ScriptableComponent::gameObject),

        // Enable/disable the component
        "enabled", &ScriptableComponent::enabled
    );

    // ===== GameObject usertype =====
    lua.new_usertype<GameObject>("GameObject",
        sol::no_constructor,  // Users can't construct directly, use atmos.world.spawn()

        // Transform properties
        "position", sol::property(
            [](GameObject* go) { return go->GetPosition(); },
            [](GameObject* go, const glm::vec3& pos) { go->SetPosition(pos); }
        ),
        "rotation", sol::property(
            [](GameObject* go) { return go->GetRotation(); },
            [](GameObject* go, const glm::vec3& rot) { go->SetRotation(rot); }
        ),
        "scale", sol::property(
            [](GameObject* go) { return go->GetScale(); },
            [](GameObject* go, const glm::vec3& scale) { go->SetScale(scale); }
        ),

        // Basic properties
        "name", sol::property(
            [](GameObject* go) { return go->GetName(); },
            [](GameObject* go, const std::string& name) { go->SetName(name); }
        ),
        "isActive", &GameObject::isActive,

        // Transform methods
        "getTransform", &GameObject::GetTransform,
        "setLocalTransform", &GameObject::SetLocalTransform,

        // Velocity (if rigidbody)
        "velocity", sol::property(
            &GameObject::GetVelocity,
            &GameObject::SetVelocity
        ),

        // Component methods
        "addMesh", [](GameObject* go, const std::string& meshName) {
            auto mesh = AssetManager::Get().GetMesh(meshName);
            if (mesh) {
                go->AddMesh(mesh);
            }
            return go;
        },

        "addLight", [](GameObject* go, sol::table props) {
            LightProps lightProps;
            lightProps.type = static_cast<LightType>(props.get_or("type", 1));
            lightProps.intensity = props.get_or("intensity", 1.0f);
            lightProps.castShadow = props.get_or("castShadow", false);

            if (auto ambient = props["ambient"]; ambient.valid()) {
                sol::table t = ambient;
                lightProps.ambient = glm::vec3(t[1], t[2], t[3]);
            }
            if (auto diffuse = props["diffuse"]; diffuse.valid()) {
                sol::table t = diffuse;
                lightProps.diffuse = glm::vec3(t[1], t[2], t[3]);
            }
            if (auto specular = props["specular"]; specular.valid()) {
                sol::table t = specular;
                lightProps.specular = glm::vec3(t[1], t[2], t[3]);
            }

            go->AddLight(lightProps);
            return go;
        },

        "addCamera", [](GameObject* go, sol::table props) {
            CameraProps camProps;
            camProps.perspective.fieldOfView = glm::radians(props.get_or("fov", 60.0f));
            camProps.perspective.aspectRatio = props.get_or("aspect", 16.0f / 9.0f);
            camProps.perspective.nearClip = props.get_or("near", 0.1f);
            camProps.perspective.farClip = props.get_or("far", 1000.0f);

            go->AddCamera(camProps);
            return go;
        },

        // Rigidbody
        "addRigidbody", [](GameObject* go, sol::table props) {
            RigidbodyProps rbProps;
            rbProps.mass = props.get_or("mass", 0.0f);
            rbProps.isKinematic = props.get_or("kinematic", false);
            rbProps.useGravity = props.get_or("gravity", true);

            go->AddRigidbody(rbProps);
            return go;
        },

        // Add a Lua script component
        "addScript", [&lua](GameObject* go, const std::string& className) {
            auto* script = new ScriptableComponent(go, lua, className);
            go->AddComponent(script);

            // Set up collision callback to forward to all ScriptableComponents
            // Capture 'go' by value (pointer copy) - safe because GameObject lifetime > callback
            go->SetCollisionCallback([go](GameObject* other) {
                // Iterate all ScriptableComponents and call their OnCollision
                // Note: GetComponent only returns first match, so we use the script directly
                // For now, just call the first ScriptableComponent's OnCollision
                // TODO: iterate all ScriptableComponents when multiple scripts per GO is supported
                auto* scriptComp = go->GetComponent<ScriptableComponent>();
                if (scriptComp) {
                    scriptComp->OnCollision(other);
                }
            });

            return go;
        },

        // Get the first ScriptableComponent (TODO: support multiple scripts per GameObject)
        "getScript", [](GameObject* go) -> ScriptableComponent* {
            return go->GetComponent<ScriptableComponent>();
        },

        "setPhysicsActivated", &GameObject::SetPhysicsActivated,

        // Utility
        sol::meta_function::to_string, [](GameObject* go) {
            return fmt::format("GameObject(\"{}\")", go->GetName());
        }
    );

    // ===== World API =====
    sol::table world = atmos.create("world");

    // Spawn a new empty GameObject
    world["spawn"] = sol::overload(
        [app]() -> GameObject* {
            return app->CreateGameObject();
        },
        [app](float x, float y, float z) -> GameObject* {
            return app->CreateGameObject(glm::vec3(x, y, z));
        },
        [app](const glm::vec3& pos) -> GameObject* {
            return app->CreateGameObject(pos);
        },
        [app](const glm::vec3& pos, const glm::vec3& rot) -> GameObject* {
            return app->CreateGameObject(pos, rot);
        },
        [app](const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale) -> GameObject* {
            return app->CreateGameObject(pos, rot, scale);
        }
    );

    // Find GameObject by name
    world["find"] = [app](const std::string& name) -> GameObject* {
        for (auto* entity : app->GetEntities()) {
            if (entity->GetName() == name) {
                return entity;
            }
        }
        return nullptr;
    };

    // Get all entities
    world["getAll"] = [app]() -> std::vector<GameObject*> {
        return app->GetEntities();
    };

    // ===== Scene API =====
    sol::table scene = atmos.create("scene");

    // Load scene from Lua table
    scene["loadTable"] = [app, &lua](sol::table sceneData) {
        // Parse entities
        if (auto entities = sceneData["entities"]; entities.valid()) {
            sol::table entitiesTable = entities;
            for (auto& [key, value] : entitiesTable) {
                std::string name = key.as<std::string>();
                sol::table entityData = value;

                // Create entity
                glm::vec3 pos(0.0f), rot(0.0f), scale(1.0f);

                if (auto p = entityData["position"]; p.valid()) {
                    sol::table t = p;
                    pos = glm::vec3(t[1], t[2], t[3]);
                }
                if (auto r = entityData["rotation"]; r.valid()) {
                    sol::table t = r;
                    rot = glm::vec3(t[1], t[2], t[3]);
                }
                if (auto s = entityData["scale"]; s.valid()) {
                    sol::table t = s;
                    scale = glm::vec3(t[1], t[2], t[3]);
                }

                auto* go = app->CreateGameObject(pos, rot, scale);
                go->SetName(name);

                // Parse components
                if (auto components = entityData["components"]; components.valid()) {
                    sol::table comps = components;

                    if (auto mesh = comps["mesh"]; mesh.valid()) {
                        sol::table meshData = mesh;
                        std::string meshName = meshData.get_or<std::string>("name", "");
                        if (!meshName.empty()) {
                            auto meshPtr = AssetManager::Get().GetMesh(meshName);
                            if (meshPtr) {
                                go->AddMesh(meshPtr);
                            }
                        }
                    }

                    if (auto camera = comps["camera"]; camera.valid()) {
                        sol::table camData = camera;
                        CameraProps props;
                        props.perspective.fieldOfView = glm::radians(camData.get_or("fov", 60.0f));
                        props.perspective.nearClip = camData.get_or("near", 0.1f);
                        props.perspective.farClip = camData.get_or("far", 1000.0f);
                        go->AddCamera(props);
                    }

                    if (auto light = comps["light"]; light.valid()) {
                        sol::table lightData = light;
                        LightProps props;
                        props.type = static_cast<LightType>(lightData.get_or("type", 1));
                        props.intensity = lightData.get_or("intensity", 1.0f);
                        go->AddLight(props);
                    }
                }
            }
        }
    };

    // Load scene from file
    scene["load"] = [app, &lua](const std::string& name) {
        std::string path = "./assets/scenes/" + name + ".lua";
        auto result = lua.safe_script_file(path, sol::script_pass_on_error);
        if (result.valid()) {
            sol::table sceneData = result;
            // Recursively call loadTable
            lua["atmos"]["scene"]["loadTable"](sceneData);
        } else {
            sol::error err = result;
            fmt::print(stderr, "[Lua Error] Failed to load scene {}: {}\n", name, err.what());
        }
    };
}

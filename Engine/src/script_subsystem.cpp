#include "script_subsystem.hpp"
#include "Atmospheric/file_system.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "game_object.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "scene.hpp"
#include <string>

ScriptSubsystem* ScriptSubsystem::_instance = nullptr;

ScriptSubsystem::ScriptSubsystem() {
    if (_instance != nullptr) throw std::runtime_error("ScriptSubsystem is already initialized!");

    _env = sol::state();

    _instance = this;
}

ScriptSubsystem::~ScriptSubsystem() {
    if (_instance == this) {
        _instance = nullptr;
    }
}

void ScriptSubsystem::Init(Application* app) {
    Subsystem::Init(app);

    _env.open_libraries();
    Source("./assets/config.lua");
    Source("./assets/manifest.lua");
    Source("./assets/main.lua");

    Run("init()");
}

void ScriptSubsystem::Process(float dt) {
    sol::protected_function updateFunc = _env["update"];
    if (updateFunc.valid()) {
        auto result = updateFunc(dt);
        if (!result.valid()) {
            sol::error err = result;
            fmt::print(stderr, "[ScriptSubsystem] Error in Lua update callback: {}\n", err.what());
        }
    }
}

void ScriptSubsystem::Bind(const std::string& func) {
}

void ScriptSubsystem::Source(const std::string& filename) {
    auto resolvedOpt = FileSystem::Get().ResolvePath(filename);
    if (!resolvedOpt) {
        fmt::print("Skip loading script file {} (not found)\n", filename);
        return;
    }
    const std::string& resolvedPath = *resolvedOpt;
    sol::protected_function_result result = _env.script_file(resolvedPath, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        std::string what = err.what();
        fmt::print("Skip loading script file {} (resolved: {})\n", filename, resolvedPath);
    }
}

void ScriptSubsystem::Run(const std::string& script) {
    sol::protected_function_result result = _env.script(script, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        std::string what = err.what();
        // TODO: handle error messages
    }
}

void ScriptSubsystem::Print(const std::string& msg) {
    Run(fmt::format("print('[ScriptSubsystem] {}')", msg));
}

sol::table ScriptSubsystem::GetData(const std::string& key) {
    return this->_env.globals()[key];
}

void ScriptSubsystem::LoadScene(int index) {
    sol::table data = GetData("scenes");
    if (!data.valid()) return;
    sol::table sceneData = data[index + 1];
    if (!sceneData.valid()) return;

    // Load textures
    sol::table texturesTable = sceneData["textures"];
    if (texturesTable.valid()) {
        std::vector<std::string> textures;
        for (const auto& kv : texturesTable) {
            sol::table textureData = kv.second;
            textures.push_back(textureData["path"].get<std::string>());
        }
        AssetManager::Get().LoadTextures(textures);
    }

    // Load shaders
    sol::table shadersTable = sceneData["shaders"];
    if (shadersTable.valid()) {
        std::unordered_map<std::string, ShaderProgramProps> shaders;
        for (const auto& [key, value] : shadersTable) {
            std::string shaderName = key.as<std::string>();
            sol::table shaderData = value;
            if (shaderData["tesc"].valid() && shaderData["tese"].valid()) {
                shaders[shaderName] = {
                    shaderData["vert"], shaderData["frag"], shaderData["tesc"], shaderData["tese"]
                };
            } else {
                shaders[shaderName] = { shaderData["vert"], shaderData["frag"] };
            }
        }
        AssetManager::Get().LoadShaders(shaders);
    }

    // Load materials
    sol::table materialsTable = sceneData["materials"];
    if (materialsTable.valid()) {
        std::vector<MaterialProps> materials;
        for (const auto& kv : materialsTable) {
            sol::table materialData = kv.second;
            int baseMapIdx = materialData.get_or("baseMapId", -1);
            int normalMapIdx = materialData.get_or("normalMapId", -1);
            int aoMapIdx = materialData.get_or("aoMapId", -1);
            int roughnessMapIdx =
                materialData.get_or("roughnessMapId", static_cast<int>(materialData.get_or("roughtnessMapId", -1)));
            int metallicMapIdx = materialData.get_or("metallicMapId", -1);
            int heightMapIdx = materialData.get_or("heightMapId", -1);

            materials.push_back(
                { .baseMap = (baseMapIdx != -1) ? TextureHandle(AssetManager::Get().GetTextureByID(baseMapIdx))
                                                : TextureHandle(),
                  .normalMap = (normalMapIdx != -1) ? TextureHandle(AssetManager::Get().GetTextureByID(normalMapIdx))
                                                    : TextureHandle(),
                  .aoMap =
                      (aoMapIdx != -1) ? TextureHandle(AssetManager::Get().GetTextureByID(aoMapIdx)) : TextureHandle(),
                  .roughnessMap = (roughnessMapIdx != -1)
                                      ? TextureHandle(AssetManager::Get().GetTextureByID(roughnessMapIdx))
                                      : TextureHandle(),
                  .metallicMap = (metallicMapIdx != -1)
                                     ? TextureHandle(AssetManager::Get().GetTextureByID(metallicMapIdx))
                                     : TextureHandle(),
                  .heightMap = (heightMapIdx != -1) ? TextureHandle(AssetManager::Get().GetTextureByID(heightMapIdx))
                                                    : TextureHandle(),
                  .diffuse =
                      glm::vec3(materialData["diffuse"][1], materialData["diffuse"][2], materialData["diffuse"][3]),
                  .specular =
                      glm::vec3(materialData["specular"][1], materialData["specular"][2], materialData["specular"][3]),
                  .ambient =
                      glm::vec3(materialData["ambient"][1], materialData["ambient"][2], materialData["ambient"][3]),
                  .shininess = static_cast<float>(materialData.get_or("shininess", 0.25)) }
            );
        }
        AssetManager::Get().LoadMaterials(materials);
    }

    // Load entities
    sol::table entitiesTable = sceneData["entities"];
    if (entitiesTable.valid()) {
        for (const auto& [eKey, eVal] : entitiesTable) {
            std::string name = eKey.as<std::string>();
            sol::table entityData = eVal;
            glm::vec3 position(0.0f);
            glm::vec3 rotation(0.0f);
            glm::vec3 scale(1.0f);

            auto pos = entityData.get_or("position", sol::table());
            if (pos.valid()) position = glm::vec3(pos[1], pos[2], pos[3]);
            auto rot = entityData.get_or("rotation", sol::table());
            if (rot.valid()) rotation = glm::vec3(rot[1], rot[2], rot[3]);
            auto sc = entityData.get_or("scale", sol::table());
            if (sc.valid()) scale = glm::vec3(sc[1], sc[2], sc[3]);

            auto* go = _app->CreateGameObject(position, rotation, scale);
            go->SetName(name);

            sol::table components = entityData["components"];
            if (components.valid()) {
                for (const auto& [cKey, cVal] : components) {
                    std::string componentType = cKey.as<std::string>();
                    sol::table componentData = cVal;
                    if (componentType == "camera") {
                        CameraProps cameraProps;
                        cameraProps.isOrthographic = false;
                        cameraProps.perspective.fieldOfView =
                            static_cast<float>(componentData.get_or("field_of_view", glm::radians(60.f)));
                        cameraProps.perspective.aspectRatio =
                            static_cast<float>(componentData.get_or("aspect_ratio", 4.f / 3.f));
                        cameraProps.perspective.nearClip =
                            static_cast<float>(componentData.get_or("near_clip_plane", 0.1f));
                        cameraProps.perspective.farClip =
                            static_cast<float>(componentData.get_or("far_clip_plane", 1000.0f));
                        cameraProps.verticalAngle = static_cast<float>(componentData.get_or("vertical_angle", 0));
                        cameraProps.horizontalAngle = static_cast<float>(componentData.get_or("horizontal_angle", 0));
                        cameraProps.eyeOffset = glm::vec3(
                            static_cast<float>(componentData.get_or("eye_offset.x", 0)),
                            static_cast<float>(componentData.get_or("eye_offset.y", 0)),
                            static_cast<float>(componentData.get_or("eye_offset.z", 0))
                        );
                        go->AddComponent<CameraComponent>(cameraProps);
                    } else if (componentType == "light") {
                        auto lightType = static_cast<LightType>(componentData.get_or("type", 1));
                        LightProps props{
                            .type = lightType,
                            .ambient = glm::vec3(
                                componentData["ambient"][1], componentData["ambient"][2], componentData["ambient"][3]
                            ),
                            .diffuse = glm::vec3(
                                componentData["diffuse"][1], componentData["diffuse"][2], componentData["diffuse"][3]
                            ),
                            .specular = glm::vec3(
                                componentData["specular"][1], componentData["specular"][2], componentData["specular"][3]
                            ),
                            .intensity = static_cast<float>(componentData.get_or("intensity", 1.0)),
                            .castShadow = static_cast<bool>(componentData.get_or("castShadow", 0))
                        };
                        if (lightType == LightType::Point) {
                            props.attenuation = glm::vec3(
                                componentData["attenuation"][1],
                                componentData["attenuation"][2],
                                componentData["attenuation"][3]
                            );
                        } else if (lightType == LightType::Directional) {
                            props.direction = glm::vec3(
                                componentData["direction"][1],
                                componentData["direction"][2],
                                componentData["direction"][3]
                            );
                        }
                        go->AddComponent<LightComponent>(props);
                    }
                }
            }
        }
    }
}

void ScriptSubsystem::GetData(const std::string& key, sol::table& data) {
    data = this->_env.globals()[key];
}

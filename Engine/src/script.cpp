#include "script.hpp"
#include "material.hpp"
#include "light_component.hpp"
#include "camera_component.hpp"
#include "scene.hpp"
#include <string>

Script* Script::_instance = nullptr;

Script::Script()
{
    if (_instance != nullptr)
        throw std::runtime_error("Script is already initialized!");

    _env = sol::state();

    _instance = this;
}

Script::~Script()
{

}

void Script::Init(Application* app)
{
    Server::Init(app);

    _env.open_libraries();
    Source("./assets/config.lua");
    Source("./assets/manifest.lua");
    Source("./assets/main.lua");

    Run("init()");
    //Bind("get_cursor_uv", &Input::GetCursorUV, Input::Get());
    //Bind("get_key_down", &Input::GetKeyDown, Input::Get());
    //Bind("check_errors", &GraphicsServer::CheckErrors, GraphicsServer::Get());
}

void Script::Process(float dt)
{
    sol::protected_function updateFunc = _env["update"];
    if (updateFunc.valid()) {
        auto result = updateFunc(dt);
        if (!result.valid()) {
            sol::error err = result;
            fmt::print(stderr, "[Script] Error in Lua update callback: {}\n", err.what());
        }
    }
}

// TODO: an extra argument is needed here
void Script::Bind(const std::string& func)
{
    //this->_env.set_function(func);
}

void Script::Source(const std::string& filename)
{
    sol::protected_function_result result = _env.script_file(filename, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        std::string what = err.what();
        fmt::print("Skip loading script file {}\n", filename);
    }
}

void Script::Run(const std::string& script)
{
    sol::protected_function_result result = _env.script(script, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        std::string what = err.what();
        // TODO: handle error messages
    }
}

void Script::Print(const std::string& msg)
{
    Run(fmt::format("print('[Script] {}')", msg));
}

sol::table Script::GetData(const std::string& key)
{
    return this->_env.globals()[key];
}

SceneDef Script::GetScene(const sol::table& sceneData)
{
    SceneDef scene;

    const sol::table texturesTable = sceneData["textures"];
    for (const auto& kv : texturesTable) {
        sol::table textureData = kv.second;
        scene.textures.push_back(textureData["path"].get<std::string>());
    }

    const sol::table shadersTable = sceneData["shaders"];
    for (const auto& [key, value] : shadersTable) {
        std::string shaderName = key.as<std::string>();
        sol::table shaderData = value;
        if (shaderData["tesc"].valid() && shaderData["tese"].valid()) {
            scene.shaders[shaderName] = { shaderData["vert"], shaderData["frag"], shaderData["tesc"], shaderData["tese"] };
        } else {
            scene.shaders[shaderName] = { shaderData["vert"], shaderData["frag"] };
        }
    }

    const sol::table materialsTable = sceneData["materials"];
    for (const auto& kv : materialsTable) {
        sol::table materialData = kv.second;
        scene.materials.push_back({
            .baseMap = (int)materialData.get_or("baseMapId", -1),
            .normalMap = (int)materialData.get_or("normalMapId", -1),
            .aoMap = (int)materialData.get_or("aoMapId", -1),
            .roughnessMap = (int)materialData.get_or("roughnessMapId", (int)materialData.get_or("roughtnessMapId", -1)),
            .metallicMap = (int)materialData.get_or("metallicMapId", -1),
            .heightMap = (int)materialData.get_or("heightMapId", -1),
            .diffuse = glm::vec3(materialData["diffuse"][1], materialData["diffuse"][2], materialData["diffuse"][3]),
            .specular = glm::vec3(materialData["specular"][1], materialData["specular"][2], materialData["specular"][3]),
            .ambient = glm::vec3(materialData["ambient"][1], materialData["ambient"][2], materialData["ambient"][3]),
            .shininess = (float)materialData.get_or("shininess", 0.25)
        });
    }

    const sol::table entitiesTable = sceneData["entities"];
    for (const auto& [eKey, eVal] : entitiesTable) {
        sol::table entityData = eVal;
        GameObjectProps def = {
            .name = eKey.as<std::string>(),
            .camera = std::nullopt,
            .light = std::nullopt
        };
        auto pos = entityData.get_or("position", sol::table());
        if (pos.valid()) {
            def.position = glm::vec3(pos[1], pos[2], pos[3]);
        }
        auto rot = entityData.get_or("rotation", sol::table());
        if (rot.valid()) {
            def.rotation = glm::vec3(rot[1], rot[2], rot[3]);
        }
        auto scale = entityData.get_or("scale", sol::table());
        if (scale.valid()) {
            def.scale = glm::vec3(scale[1], scale[2], scale[3]);
        }
        sol::table components = entityData["components"];
        for (const auto& [cKey, cVal] : components) {
            std::string componentType = cKey.as<std::string>();
            sol::table componentData = cVal;
            if (componentType == "camera") {
                CameraProps cameraProps;
                cameraProps.isOrthographic = false;
                cameraProps.perspective.fieldOfView = (float)componentData.get_or("field_of_view", glm::radians(60.f));
                cameraProps.perspective.aspectRatio = (float)componentData.get_or("aspect_ratio", 4.f / 3.f);
                cameraProps.perspective.nearClip = (float)componentData.get_or("near_clip_plane", 0.1f);
                cameraProps.perspective.farClip = (float)componentData.get_or("far_clip_plane", 1000.0f);
                cameraProps.verticalAngle = (float)componentData.get_or("vertical_angle", 0);
                cameraProps.horizontalAngle = (float)componentData.get_or("horizontal_angle", 0);
                cameraProps.eyeOffset = glm::vec3(
                    (float)componentData.get_or("eye_offset.x", 0),
                    (float)componentData.get_or("eye_offset.y", 0),
                    (float)componentData.get_or("eye_offset.z", 0)
                );
                def.camera = cameraProps;
            } else if (componentType == "light") {
                auto lightType = static_cast<LightType>(componentData.get_or("type", 1));
                def.light = LightProps{
                    .type = lightType,
                    .ambient = glm::vec3(
                        componentData["ambient"][1],
                        componentData["ambient"][2],
                        componentData["ambient"][3]
                    ),
                    .diffuse = glm::vec3(
                        componentData["diffuse"][1],
                        componentData["diffuse"][2],
                        componentData["diffuse"][3]
                    ),
                    .specular = glm::vec3(
                        componentData["specular"][1],
                        componentData["specular"][2],
                        componentData["specular"][3]
                    ),
                    .intensity = (float)componentData.get_or("intensity", 1.0),
                    .castShadow = (bool)componentData.get_or("castShadow", 0)
                };
                if (lightType == LightType::Point) {
                    def.light->attenuation = glm::vec3(
                        componentData["attenuation"][1],
                        componentData["attenuation"][2],
                        componentData["attenuation"][3]
                    );
                } else if (lightType == LightType::Directional) {
                    def.light->direction = glm::vec3(
                        componentData["direction"][1],
                        componentData["direction"][2],
                        componentData["direction"][3]
                    );
                }
            }
        }
        scene.gameObjects.push_back(def);
    }
    return scene;
}

std::vector<SceneDef> Script::GetScenes()
{
    std::vector<SceneDef> scenes;

    sol::table data = GetData(std::string("scenes"));
    for (const auto& kv : data) {
        sol::table sceneData = kv.second;
        scenes.push_back(GetScene(sceneData));
    }

    return scenes;
}

void Script::GetData(const std::string& key, sol::table& data)
{
    data = this->_env.globals()[key];
}

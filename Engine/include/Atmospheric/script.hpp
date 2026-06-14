#pragma once
#include "globals.hpp"
#include "server.hpp"
#include "scene.hpp"

//#define SOL_ALL_SAFETIES_ON 1
#define SOL_LUA_VERSION 504
#include "sol/sol.hpp"


class Script : public Server
{
private:
    static Script* _instance;

public:
    static Script* Get()
    {
        return _instance;
    }

    Script();

    ~Script();

    void Init(Application* app) override;

    void Process(float dt) override;

    void Bind(const std::string& func);

    void Source(const std::string& filename);

    void Run(const std::string&);

    void Print(const std::string& msg);

    sol::table GetData(const std::string& key);

    SceneDef GetScene(const sol::table& sceneData);

    std::vector<SceneDef> GetScenes();

    void GetData(const std::string& key, sol::table& data);

private:
    Application* _app;
    sol::state _env;
};
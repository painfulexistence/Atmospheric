#pragma once
#include "globals.hpp"
#include "subsystem.hpp"
#include "scene.hpp"

//#define SOL_ALL_SAFETIES_ON 1
#define SOL_LUA_VERSION 504
#include "sol/sol.hpp"


class ScriptSubsystem : public Subsystem
{
private:
    static ScriptSubsystem* _instance;

public:
    static ScriptSubsystem* Get()
    {
        return _instance;
    }

    ScriptSubsystem();

    ~ScriptSubsystem();

    void Init(Application* app) override;

    void Process(float dt) override;

    void Bind(const std::string& func);

    void Source(const std::string& filename);

    void Run(const std::string&);

    void Print(const std::string& msg);

    sol::table GetData(const std::string& key);

    void LoadScene(int index);

    void GetData(const std::string& key, sol::table& data);

private:
    Application* _app = nullptr;
    sol::state _env;
};
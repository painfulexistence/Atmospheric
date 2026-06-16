#pragma once
#include "globals.hpp"
#include <string>
#include <unordered_map>
#include <functional>
#include "config.hpp"
#include "server.hpp"

struct LogEntry {
    std::string message;
};

/// Logging and command palette system
class Console : public Server
{
private:
    static Console* _instance;

public:
    static Console* Get()
    {
        return _instance;
    }

    Console();
    ~Console();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

    void RegisterCommand(const std::string& cmd, std::function<void(const std::vector<std::string>&)> callback);
    void ExecuteCommand(const std::string& cmd);

private:
    bool _showInfo = true;
    bool _showWarn = true;
    bool _showError = true;

    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> _commands;
};

#define LOG(msg, ...) Console::Get()->Info(fmt::format(msg, ##__VA_ARGS__))
#define ENGINE_LOG(msg, ...) Console::Get()->Info("[Engine] " + fmt::format(msg, ##__VA_ARGS__))
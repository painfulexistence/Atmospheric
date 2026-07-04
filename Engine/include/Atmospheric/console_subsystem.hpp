#pragma once
#include "config.hpp"
#include "globals.hpp"
#include "subsystem.hpp"
#include <functional>
#include <string>
#include <unordered_map>

struct LogEntry {
    std::string message;
};

/// Logging and command palette system
class ConsoleSubsystem : public Subsystem {
private:
    static ConsoleSubsystem* _instance;

public:
    static ConsoleSubsystem* Get() {
        return _instance;
    }

    ConsoleSubsystem();
    ~ConsoleSubsystem();

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

#define LOG(msg, ...) ConsoleSubsystem::Get()->Info(fmt::format(msg, ##__VA_ARGS__))
#define ENGINE_LOG(msg, ...) ConsoleSubsystem::Get()->Info("[Engine] " + fmt::format(msg, ##__VA_ARGS__))
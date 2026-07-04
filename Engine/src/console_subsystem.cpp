#include "console_subsystem.hpp"
#include "log.hpp"
#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#else
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#endif

ConsoleSubsystem* ConsoleSubsystem::_instance = nullptr;

ConsoleSubsystem::ConsoleSubsystem() {
    if (_instance != nullptr) throw std::runtime_error("ConsoleSubsystem is already initialized!");

    _instance = this;
}

ConsoleSubsystem::~ConsoleSubsystem() {
    if (_instance == this) {
        _instance = nullptr;
    }
}

void ConsoleSubsystem::Init(Application* app) {
    Subsystem::Init(app);

#ifndef __EMSCRIPTEN__
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("console", consoleSink);
    spdlog::set_default_logger(logger);
#endif
}

void ConsoleSubsystem::Process(float dt) {
}

void ConsoleSubsystem::DrawImGui(float dt) {
    if (ImGui::CollapsingHeader("Console", ImGuiTreeNodeFlags_DefaultOpen)) {
#ifndef __EMSCRIPTEN__
        ImGui::Text("Log Level:");
        if (ImGui::RadioButton("Info", spdlog::level::info == spdlog::default_logger()->level())) {
            spdlog::default_logger()->set_level(spdlog::level::info);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Warn", spdlog::level::warn == spdlog::default_logger()->level())) {
            spdlog::default_logger()->set_level(spdlog::level::warn);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Error", spdlog::level::err == spdlog::default_logger()->level())) {
            spdlog::default_logger()->set_level(spdlog::level::err);
        }

        ImGui::Separator();
#endif

        ImGui::BeginChild("Log", ImVec2(0, 300));
        ImGui::Text("Log:");
        ImGui::EndChild();

        ImGui::Separator();

        // TODO: command palette
        static char gcommand[256] = "";
        if (ImGui::InputText("Command", gcommand, IM_ARRAYSIZE(gcommand), ImGuiInputTextFlags_EnterReturnsTrue)) {
            ExecuteCommand(gcommand);
            gcommand[0] = '\0';
        }
    }
}

// These forward to Log, the single logging dispatch point. "{}" passes the
// message as data, so any braces it contains are not treated as format specs.
void ConsoleSubsystem::Info(const std::string& message) {
    Log::Info("{}", message);
}

void ConsoleSubsystem::Warn(const std::string& message) {
    Log::Warn("{}", message);
}

void ConsoleSubsystem::Error(const std::string& message) {
    Log::Error("{}", message);
}

void ConsoleSubsystem::RegisterCommand(
    const std::string& cmd, std::function<void(const std::vector<std::string>&)> callback
) {
    _commands[cmd] = callback;
}

void ConsoleSubsystem::ExecuteCommand(const std::string& cmd) {
    auto it = _commands.find(cmd);
    if (it != _commands.end()) {
        it->second({});
    }

    // ScriptSubsystem::Get()->Run(cmd); // ScriptSubsystem system refactored
}
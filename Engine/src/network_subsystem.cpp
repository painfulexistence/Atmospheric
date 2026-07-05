#include "network_subsystem.hpp"
#include "application.hpp"
#include <imgui.h>
#include <spdlog/spdlog.h>

void NetworkSubsystem::Init(Application* app) {
    Subsystem::Init(app);
    spdlog::info("NetworkSubsystem: initialized");
}

void NetworkSubsystem::Process(float /*dt*/) {
    _http.Pump();
    _ws.Pump();
}

void NetworkSubsystem::DrawImGui(float /*dt*/) {
#ifndef NDEBUG
    if (!ImGui::CollapsingHeader("NetworkSubsystem")) return;
    ImGui::Text("HTTP active requests : %d", _http.ActiveRequestCount());
    ImGui::Text("WS connections       : %d", _ws.ConnectionCount());
#endif
}

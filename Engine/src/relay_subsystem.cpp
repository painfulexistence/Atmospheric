#include "relay_subsystem.hpp"

#ifndef __EMSCRIPTEN__

#include "application.hpp"
#include "logging.hpp"
#include <imgui.h>

void RelaySubsystem::Init(Application* app) {
    Subsystem::Init(app);
    ENGINE_INFO("RelaySubsystem: initialized");
}

void RelaySubsystem::Process(float dt) {
    _relay.Process(dt);
}

void RelaySubsystem::DrawImGui(float /*dt*/) {
#ifndef NDEBUG
    if (!ImGui::CollapsingHeader("RelaySubsystem")) return;
    ImGui::Text("Running : %s", _relay.IsRunning() ? "yes" : "no");
    ImGui::Text("Port    : %u", _relay.BoundPort());
    ImGui::Text("Rooms   : %d", _relay.RoomCount());
#endif
}

#endif// !__EMSCRIPTEN__

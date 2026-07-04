#include "subsystem.hpp"
#include "application.hpp"

void Subsystem::Init(Application* app) {
    _app = app;
    _initialized = true;
}

void Subsystem::Process(float dt) {
    // Default processing
}

void Subsystem::DrawImGui(float dt) {
}
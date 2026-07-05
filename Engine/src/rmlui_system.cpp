#include "rmlui_system.hpp"
#include "console_subsystem.hpp"
#include "log.hpp"
#include "window.hpp"
#include <fmt/format.h>

RmlUiSystem::RmlUiSystem() {
}

RmlUiSystem::~RmlUiSystem() {
}

double RmlUiSystem::GetElapsedTime() {
    return Window::Get()->GetTime();
}

bool RmlUiSystem::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    std::string msg = fmt::format("[RmlUi] {}", message);
    switch (type) {
    case Rml::Log::LT_ALWAYS:
    case Rml::Log::LT_ERROR:
        Log::Error("{}", msg);
        break;
    case Rml::Log::LT_WARNING:
        Log::Warn("{}", msg);
        break;
    case Rml::Log::LT_INFO:
        Log::Info("{}", msg);
        break;
    case Rml::Log::LT_DEBUG:
        // Log::Info("{}", msg); // Treat debug as info or ignore?
        break;
    default:
        break;
    }
    return true;
}

void RmlUiSystem::SetMouseCursor(const Rml::String& cursor_name) {
    Window::Get()->SetMouseCursor(cursor_name);
}

void RmlUiSystem::SetClipboardText(const Rml::String& text) {
    Window::Get()->SetClipboardText(text);
}

void RmlUiSystem::GetClipboardText(Rml::String& text) {
    text = Window::Get()->GetClipboardText();
}

void RmlUiSystem::ActivateKeyboard(Rml::Vector2f caret_position, float line_height) {
    // On mobile platforms, this would show the virtual keyboard
    // For desktop, this is typically a no-op
}

void RmlUiSystem::DeactivateKeyboard() {
    // On mobile platforms, this would hide the virtual keyboard
}

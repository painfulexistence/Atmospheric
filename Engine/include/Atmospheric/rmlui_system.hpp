#pragma once
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

class RmlUiSystem : public Rml::SystemInterface {
public:
    RmlUiSystem();
    ~RmlUiSystem() override;

    // Get the number of seconds elapsed since the start of the application
    double GetElapsedTime() override;

    // Log a message to the application
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    // Set mouse cursor
    void SetMouseCursor(const Rml::String& cursor_name) override;

    // Set clipboard text
    void SetClipboardText(const Rml::String& text) override;

    // Get clipboard text
    void GetClipboardText(Rml::String& text) override;

    // Activate keyboard
    void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;

    // Deactivate keyboard
    void DeactivateKeyboard() override;

private:
    double m_start_time;
};

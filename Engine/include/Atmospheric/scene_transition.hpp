#pragma once

namespace Rml {
    class ElementDocument;
}

// Loading-screen / transition controller.
//
// Responsibilities are strictly presentation: show and fade the loading overlay
// in when a scene transition begins, tick the fade-out timer each frame, hide it
// once the fade completes, and (later) drive a progress bar.
//
// It performs NO asset loading and knows nothing about scenes, textures, or
// shaders — Application owns all of that (see Application::GoScene).
class SceneTransition {
public:
    // Load the loading-screen RML document. Call once, after RmlUiManager has
    // been initialized.
    void Init();

    // Begin a transition: show the overlay and fade it in.
    void Begin();

    // End a transition: fade the overlay out. The document is hidden once the
    // fade-out timer elapses (see Update).
    void End();

    // Tick the fade-out timer; hides the document when it reaches zero. Safe to
    // call every frame regardless of whether a transition is in progress.
    void Update(float dt);

    // Set the progress bar fill in [0, 1]. No-op if the loading-screen document
    // has no "loading-progress" element (reserved for future use).
    void SetProgress(float fraction);

private:
    Rml::ElementDocument* _doc = nullptr;
    float _fadeOutTimer = 0.0f;
    static constexpr float kFadeDuration = 0.5f;
};

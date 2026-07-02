#include "scene_transition.hpp"
#include "rmlui_manager.hpp" // pulls in <RmlUi/Core.h> (ElementDocument, Element)

void SceneTransition::Init()
{
    _doc = RmlUiManager::Get()->LoadDocument("assets/ui/loading_screen.rml");
}

void SceneTransition::Begin()
{
    if (!_doc) return;
    _fadeOutTimer = 0.0f;
    _doc->Show();
    if (auto* body = _doc->GetElementById("loading-screen"))
        body->SetClass("visible", true);
}

void SceneTransition::End()
{
    if (!_doc) return;
    if (auto* body = _doc->GetElementById("loading-screen"))
        body->SetClass("visible", false);
    // Keep the document shown until the CSS fade-out finishes, then hide it in
    // Update so it stops consuming input / draw time.
    _fadeOutTimer = kFadeDuration;
}

void SceneTransition::Update(float dt)
{
    if (_fadeOutTimer > 0.0f) {
        _fadeOutTimer -= dt;
        if (_fadeOutTimer <= 0.0f && _doc)
            _doc->Hide();
    }
}

void SceneTransition::SetProgress(float fraction)
{
    if (!_doc) return;
    if (auto* fill = _doc->GetElementById("loading-progress")) {
        fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        fill->SetProperty("width", std::to_string(fraction * 100.0f) + "%");
    }
}

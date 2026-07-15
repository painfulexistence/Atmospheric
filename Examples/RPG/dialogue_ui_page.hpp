#pragma once
// DialogueBox — an RmlUi document driven through the engine's UIPageComponent.
//
// This is the piece that seeds a text-heavy / narrative game: the body text is
// laid out and word-wrapped by RmlUi (see dialogue.rcss), while gameplay only
// pushes lines in and drives a typewriter reveal. Advancing / hiding is owned by
// the caller (the RPG on the E key + a timer), matching how a DialogueSystem
// would eventually drive it.
#include "Atmospheric.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <string>

class DialogueUIPage : public UIPageComponent {
public:
    using UIPageComponent::UIPageComponent;// inherit (GameObject*, documentPath)

    std::string GetName() const override {
        return "DialogueUIPage";
    }

    // Begin revealing a line. speaker is shown as-is; text reveals over time.
    void ShowLine(const std::string& speaker, const std::string& text) {
        _full = text;
        _shown = 0;
        _revealT = 0.0f;
        _active = true;
        if (_speakerEl) _speakerEl->SetInnerRML(speaker);
        if (_textEl) _textEl->SetInnerRML("");
        if (_moreEl) _moreEl->SetProperty("visibility", "hidden");
        Show();
    }

    void HideBox() {
        _active = false;
        Hide();
    }

    bool IsActive() const {
        return _active;
    }
    bool IsRevealComplete() const {
        return _shown >= static_cast<int>(_full.size());
    }

    // Snap the reveal to the end (e.g. player pressed advance mid-reveal).
    void CompleteReveal() {
        _shown = static_cast<int>(_full.size());
        if (_textEl) _textEl->SetInnerRML(_full);
        if (_moreEl) _moreEl->SetProperty("visibility", "visible");
    }

protected:
    void OnDocumentLoaded() override {
        _speakerEl = GetElement("dialogue_speaker");
        _textEl = GetElement("dialogue_text");
        _moreEl = GetElement("dialogue_more");
        Hide();// start hidden; ShowLine() reveals it
    }

    void OnTick(float dt) override {
        if (!_active || IsRevealComplete()) return;
        _revealT += dt * _charsPerSec;
        int target = std::min(static_cast<int>(_full.size()), static_cast<int>(_revealT));
        if (target != _shown) {
            _shown = target;
            if (_textEl) _textEl->SetInnerRML(_full.substr(0, _shown));
            if (IsRevealComplete() && _moreEl) _moreEl->SetProperty("visibility", "visible");
        }
    }

private:
    Rml::Element* _speakerEl = nullptr;
    Rml::Element* _textEl = nullptr;
    Rml::Element* _moreEl = nullptr;

    std::string _full;
    int _shown = 0;
    float _revealT = 0.0f;
    float _charsPerSec = 45.0f;
    bool _active = false;
};

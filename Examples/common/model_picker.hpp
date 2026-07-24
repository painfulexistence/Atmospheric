#pragma once
// Shared model-picker HUD for GLTFViewer / USDViewer.
//
// A cross-platform, VFS-driven picker: it lists the models actually available to
// the app (FileSystem::List over assets/models — real disk on native, a fetch
// manifest on web) and renders them in an RmlUi <select>. This runs the same
// everywhere, including sandboxed web / mobile where an OS file dialog can't
// browse the asset bundle.
//
// IMPORTANT: RmlUi fires a <select> "change" ASYNCHRONOUSLY when its options are
// first populated (during a later Context::Update, not inside OnDocumentLoaded).
// A fresh HUD therefore emits one spurious "change" for its default selection.
// If that were treated as a user pick it would reload the scene, which recreates
// the HUD, which fires another spurious change — an infinite reload loop. So the
// change handler dedups against the value we last acted on (seeded from the
// initial selection); only a genuine change to a different model calls onPick.
// The owner must also DEFER the actual scene reload (stash the path, act next
// OnUpdate) rather than tearing the document down from inside its own dispatch.
#include <Atmospheric.hpp>

#include <RmlUi/Core/Elements/ElementFormControlSelect.h>

#include <functional>
#include <string>
#include <vector>

class ModelPickerHud : public UIPageComponent {
public:
    // `files` are virtual paths (FileSystem::List); `current` is the model the
    // scene already shows (pre-selected in the dropdown, "" if none); `onPick`
    // is invoked with the chosen path only on a real user change.
    ModelPickerHud(
        GameObject* owner, std::vector<std::string> files, std::string current, std::function<void(std::string)> onPick
    )
      : UIPageComponent(owner, "assets/ui/model_picker.rml"), _files(std::move(files)), _current(std::move(current)),
        _onPick(std::move(onPick)) {
    }

    std::string GetName() const override {
        return "ModelPickerHud";
    }

protected:
    void OnDocumentLoaded() override {
        _select = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(GetElement("model_select"));
        if (_select) {
            int currentIndex = -1;
            for (size_t i = 0; i < _files.size(); ++i) {
                _select->Add(FileSystem::BaseName(_files[i]).c_str(), _files[i].c_str());// label = name, value = path
                if (_files[i] == _current) currentIndex = static_cast<int>(i);
            }
            // Reflect the loaded model in the dropdown; otherwise the default is
            // the first option. Either way, seed _lastValue from the resulting
            // selection so the async initial "change" is a no-op.
            if (currentIndex >= 0) _select->SetSelection(currentIndex);
            _lastValue = _select->GetValue();

            AddListener(_select, "change", [this] {
                if (!_select || !_onPick) return;
                const std::string value = _select->GetValue();
                if (value.empty() || value == _lastValue) return;// spurious / no-op selection
                _lastValue = value;
                _onPick(value);
            });
        }
        if (Rml::Element* count = GetElement("count"))
            count->SetInnerRML(std::to_string(_files.size()) + " in assets/models");
    }

private:
    std::vector<std::string> _files;
    std::string _current;
    std::function<void(std::string)> _onPick;
    Rml::ElementFormControlSelect* _select = nullptr;
    std::string _lastValue;
};

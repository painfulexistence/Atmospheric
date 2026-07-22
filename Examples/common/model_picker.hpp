#pragma once
// Shared model-picker HUD for GLTFViewer / USDViewer.
//
// A cross-platform, VFS-driven picker: it lists the models actually available to
// the app (FileSystem::List over assets/models — real disk on native, MEMFS on
// web, an app bundle later) and renders them in an RmlUi <select>. This runs the
// same everywhere, including sandboxed web / mobile where an OS file dialog can't
// browse the asset bundle.
//
// The <select> "change" event fires on the main thread during UI update, but the
// owner should DEFER the actual scene reload (stash the path, act on it next
// OnUpdate) rather than tearing the document down from inside its own event
// dispatch.
#include <Atmospheric.hpp>

#include <RmlUi/Core/Elements/ElementFormControlSelect.h>

#include <functional>
#include <string>
#include <vector>

// A dropdown of model files + a small label. `files` are virtual paths (from
// FileSystem::List); `onPick` is invoked with the chosen path when the user
// changes the selection. Programmatic population does not fire onPick.
class ModelPickerHud : public UIPageComponent {
public:
    ModelPickerHud(GameObject* owner, std::vector<std::string> files, std::function<void(std::string)> onPick)
      : UIPageComponent(owner, "assets/ui/model_picker.rml"), _files(std::move(files)), _onPick(std::move(onPick)) {
    }

    std::string GetName() const override {
        return "ModelPickerHud";
    }

protected:
    void OnDocumentLoaded() override {
        _select = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(GetElement("model_select"));
        if (_select) {
            for (const std::string& f : _files)
                _select->Add(FileSystem::BaseName(f).c_str(), f.c_str());// label = filename, value = virtual path
            // Fires for real user changes only; the initial population above
            // happens before _ready flips, so it is ignored.
            AddListener(_select, "change", [this] {
                if (_ready && _select && _onPick) {
                    const std::string value = _select->GetValue();
                    if (!value.empty()) _onPick(value);
                }
            });
        }
        if (Rml::Element* count = GetElement("count"))
            count->SetInnerRML(std::to_string(_files.size()) + " in assets/models");
        _ready = true;
    }

private:
    std::vector<std::string> _files;
    std::function<void(std::string)> _onPick;
    Rml::ElementFormControlSelect* _select = nullptr;
    bool _ready = false;
};

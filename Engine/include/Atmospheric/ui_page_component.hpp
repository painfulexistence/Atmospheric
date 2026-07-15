#pragma once
#include "component.hpp"
#include <RmlUi/Core/ElementDocument.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A UI page backed by an RmlUi document, driven through the standard component
// lifecycle instead of a parallel manager. Attach one to a GameObject with the
// .rml path; override OnDocumentLoaded() to look up elements and wire event
// listeners via the AddListener helpers, which own the listener adapters and
// tear them down on detach — so games never hand-roll Rml::EventListener glue.
//
//   auto* hud = uiGO->AddComponent<MyHud>("assets/ui/hud.rml");  // MyHud : UIPageComponent
//
class UIPageComponent : public Component {
public:
    UIPageComponent(GameObject* owner, std::string documentPath);
    ~UIPageComponent() override;

    std::string GetName() const override {
        return "UIPage";
    }
    void OnAttach() override;
    void OnDetach() override;

    Rml::ElementDocument* GetDocument() const {
        return _document;
    }
    Rml::Element* GetElement(const std::string& id) const;

    // Forward an element event to a std::function. The adapter is owned here and
    // outlives the element automatically (released on detach).
    void AddListener(Rml::Element* element, const std::string& event, std::function<void()> callback);
    void AddListener(const std::string& elementId, const std::string& event, std::function<void()> callback);

    void Show();
    void Hide();
    bool IsVisible() const;

protected:
    // Called once the document has loaded and been shown. Override to resolve
    // elements and register listeners.
    virtual void OnDocumentLoaded() {
    }

private:
    class Listener;// Rml::EventListener → std::function adapter, defined in the .cpp

    std::string _documentPath;
    Rml::ElementDocument* _document = nullptr;
    std::vector<std::unique_ptr<Listener>> _listeners;
};

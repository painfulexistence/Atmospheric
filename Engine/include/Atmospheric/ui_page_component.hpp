#pragma once
#include "component.hpp"
#include "ui_navigator.hpp"
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
// Visibility is INTENT (ported from Vapor's page layer): Show()/Hide() record
// the target state and call the OnShow()/OnHide() hooks — override those to
// run an animated transition (e.g. a Tween-driven fade or an RCSS class
// toggle) instead of the default hard show/hide. With lazyLoad, the document
// isn't loaded until the first Show().
//
// The component implements IUIPage, so it can be registered with UINavigator
// for stack navigation; OnDetach unregisters it automatically.
//
// Reload contract: OnDocumentLoaded() may run MORE THAN ONCE per component —
// Reload() closes and re-opens the document for RML hot editing. Cache
// Rml::Element pointers only inside OnDocumentLoaded(); a reload invalidates
// every previous one (AddListener adapters are torn down for you).
class UIPageComponent : public Component, public IUIPage {
public:
    UIPageComponent(GameObject* owner, std::string documentPath, bool lazyLoad = false);
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
    // outlives the element automatically (released on detach/reload).
    void AddListener(Rml::Element* element, const std::string& event, std::function<void()> callback);
    void AddListener(const std::string& elementId, const std::string& event, std::function<void()> callback);

    // Visibility intent (IUIPage). Show() loads a lazy document on first use.
    void Show() override;
    void Hide() override;
    bool IsVisible() const;
    bool GetTargetVisible() const {
        return _targetVisible;
    }

    // Close and re-open the document from the same path (RML hot reload):
    // listeners are torn down first, then OnDocumentLoaded() re-fires against
    // the fresh DOM, and the previous visibility intent is re-applied.
    void Reload();

protected:
    // Called once the document has loaded. Override to resolve elements and
    // register listeners. May be called again after Reload().
    virtual void OnDocumentLoaded() {
    }

    // Transition hooks: the default is a hard document Show/Hide. Override to
    // animate (start a fade tween, toggle an RCSS class) — the document stays
    // loaded either way, and _targetVisible carries the intent.
    virtual void OnShow();
    virtual void OnHide();

private:
    class Listener;// Rml::EventListener → std::function adapter, defined in the .cpp

    bool EnsureLoaded();// load + OnDocumentLoaded if not yet loaded
    void ClearListeners();

    std::string _documentPath;
    Rml::ElementDocument* _document = nullptr;
    std::vector<std::unique_ptr<Listener>> _listeners;
    bool _lazyLoad = false;
    bool _targetVisible = false;
};

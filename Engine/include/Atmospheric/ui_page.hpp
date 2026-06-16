#pragma once
#include <RmlUi/Core/ElementDocument.h>

class UIPageManager;

class UIPage {
public:
    virtual ~UIPage() = default;

    // Called once the RmlUi document has been loaded and assigned
    virtual void OnAttach() {}
    // Called just before the document is unloaded and the page is removed
    virtual void OnDetach() {}
    // Called each time the page transitions to visible
    virtual void OnShow() {}
    // Called each time the page transitions to hidden
    virtual void OnHide() {}
    // Called every frame while the page is visible
    virtual void OnUpdate(float dt) {}

    Rml::ElementDocument* GetDocument() const {
        return _document;
    }

    bool IsVisible() const {
        return _document && _document->IsVisible();
    }

protected:
    Rml::ElementDocument* _document = nullptr;

    friend class UIPageManager;
};

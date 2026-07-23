#include "ui_page_component.hpp"
#include "rmlui_manager.hpp"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

// std::function adapter — the boilerplate every RmlUi caller used to hand-roll.
// Tracks the element it is attached to via OnAttach/OnDetach so teardown is
// safe in BOTH orders: if the document died first, Rml already called
// OnDetach (element_ is null) and we must not touch the dead element; if the
// component detaches first, we remove ourselves from the live element before
// freeing (otherwise the element's later destruction would call OnDetach on a
// freed adapter).
class UIPageComponent::Listener : public Rml::EventListener {
public:
    Listener(std::string event, std::function<void()> cb) : _event(std::move(event)), _cb(std::move(cb)) {
    }
    void ProcessEvent(Rml::Event& /*event*/) override {
        if (_cb) _cb();
    }
    void OnAttach(Rml::Element* element) override {
        _element = element;
    }
    void OnDetach(Rml::Element* /*element*/) override {
        _element = nullptr;
    }

    Rml::Element* GetElement() const {
        return _element;
    }
    const std::string& GetEvent() const {
        return _event;
    }

private:
    Rml::Element* _element = nullptr;
    std::string _event;
    std::function<void()> _cb;
};

UIPageComponent::UIPageComponent(GameObject* owner, std::string documentPath, bool lazyLoad)
    : _documentPath(std::move(documentPath)), _lazyLoad(lazyLoad) {
    gameObject = owner;
}

// Out-of-line so unique_ptr<Listener> sees the complete type.
UIPageComponent::~UIPageComponent() = default;

bool UIPageComponent::EnsureLoaded() {
    if (_document) return true;
    auto* rml = RmlUiManager::Get();
    if (!rml) return false;
    _document = rml->LoadDocument(_documentPath);
    if (_document) OnDocumentLoaded();
    return _document != nullptr;
}

void UIPageComponent::ClearListeners() {
    // Remove adapters from elements that are still alive, then free them (see
    // the Listener comment for why both halves matter).
    for (auto& l : _listeners)
        if (l->GetElement()) l->GetElement()->RemoveEventListener(l->GetEvent(), l.get());
    _listeners.clear();
}

void UIPageComponent::OnAttach() {
    if (_lazyLoad) return;// loaded (and shown) on the first Show()
    if (EnsureLoaded()) Show();
}

void UIPageComponent::OnDetach() {
    if (auto* nav = UINavigator::Get()) nav->Unregister(this);
    ClearListeners();// detach listeners before the document they point into dies
    if (_document) {
        RmlUiManager::Get()->UnloadDocument(_document);
        _document = nullptr;
    }
}

Rml::Element* UIPageComponent::GetElement(const std::string& id) const {
    return _document ? _document->GetElementById(id) : nullptr;
}

void UIPageComponent::AddListener(Rml::Element* element, const std::string& event, std::function<void()> callback) {
    if (!element) return;
    auto listener = std::make_unique<Listener>(event, std::move(callback));
    element->AddEventListener(event, listener.get());
    _listeners.push_back(std::move(listener));
}

void UIPageComponent::AddListener(
    const std::string& elementId, const std::string& event, std::function<void()> callback
) {
    AddListener(GetElement(elementId), event, std::move(callback));
}

void UIPageComponent::Show() {
    _targetVisible = true;
    if (!EnsureLoaded()) return;
    OnShow();
}

void UIPageComponent::Hide() {
    _targetVisible = false;
    if (!_document) return;
    OnHide();
}

void UIPageComponent::OnShow() {
    if (_document) _document->Show();
}

void UIPageComponent::OnHide() {
    if (_document) _document->Hide();
}

bool UIPageComponent::IsVisible() const {
    return _document && _document->IsVisible();
}

void UIPageComponent::Reload() {
    auto* rml = RmlUiManager::Get();
    if (!rml) return;
    const bool wasVisible = _targetVisible;

    // Listeners first — their elements must still be alive for removal.
    ClearListeners();
    if (_document) {
        rml->UnloadDocument(_document);
        _document = nullptr;
    }

    if (!EnsureLoaded()) return;// broken edit: stays unloaded, next Show() retries
    if (wasVisible)
        Show();
    else
        Hide();
}

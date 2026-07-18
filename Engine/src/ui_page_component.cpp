#include "ui_page_component.hpp"
#include "rmlui_manager.hpp"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

// std::function adapter — the boilerplate every RmlUi caller used to hand-roll.
class UIPageComponent::Listener : public Rml::EventListener {
public:
    explicit Listener(std::function<void()> cb) : _cb(std::move(cb)) {
    }
    void ProcessEvent(Rml::Event& /*event*/) override {
        if (_cb) _cb();
    }

private:
    std::function<void()> _cb;
};

UIPageComponent::UIPageComponent(GameObject* owner, std::string documentPath) : _documentPath(std::move(documentPath)) {
    gameObject = owner;
}

// Out-of-line so unique_ptr<Listener> sees the complete type.
UIPageComponent::~UIPageComponent() = default;

void UIPageComponent::OnAttach() {
    _document = RmlUiManager::Get()->LoadDocument(_documentPath);
    if (_document) {
        _document->Show();
        OnDocumentLoaded();
    }
}

void UIPageComponent::OnDetach() {
    _listeners.clear();// detach listeners before the document they point into dies
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
    auto listener = std::make_unique<Listener>(std::move(callback));
    element->AddEventListener(event, listener.get());
    _listeners.push_back(std::move(listener));
}

void UIPageComponent::AddListener(
    const std::string& elementId, const std::string& event, std::function<void()> callback
) {
    AddListener(GetElement(elementId), event, std::move(callback));
}

void UIPageComponent::Show() {
    if (_document) _document->Show();
}

void UIPageComponent::Hide() {
    if (_document) _document->Hide();
}

bool UIPageComponent::IsVisible() const {
    return _document && _document->IsVisible();
}

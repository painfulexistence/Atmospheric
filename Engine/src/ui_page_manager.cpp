#include "ui_page_manager.hpp"
#include "rmlui_manager.hpp"
#include <spdlog/spdlog.h>

UIPageManager* UIPageManager::s_instance = nullptr;

UIPageManager* UIPageManager::Get() {
    if (!s_instance) {
        s_instance = new UIPageManager();
    }
    return s_instance;
}

UIPageManager::UIPageManager() {
    s_instance = this;
}

UIPageManager::~UIPageManager() {
    for (auto& [id, entry] : _pages) {
        if (entry.page->_document) {
            RmlUiManager::Get()->UnloadDocument(entry.page->_document);
            entry.page->_document = nullptr;
        }
        entry.page->OnDetach();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void UIPageManager::_loadDocument(const UIPageID& id) {
    auto it = _pages.find(id);
    if (it == _pages.end()) return;

    PageEntry& entry = it->second;
    Rml::ElementDocument* doc = RmlUiManager::Get()->LoadDocument(entry.documentPath);
    if (!doc) {
        spdlog::warn("UIPageManager: failed to load document for page '{}' ({})", id, entry.documentPath);
        return;
    }

    entry.page->_document = doc;
    entry.page->OnAttach();
}

void UIPageManager::RemovePage(const UIPageID& id) {
    auto it = _pages.find(id);
    if (it == _pages.end()) return;

    PageEntry& entry = it->second;
    if (entry.page->_document) {
        RmlUiManager::Get()->UnloadDocument(entry.page->_document);
        entry.page->_document = nullptr;
    }
    entry.page->OnDetach();

    // Remove from navigation stack if present
    _navigationStack.erase(
      std::remove(_navigationStack.begin(), _navigationStack.end(), id),
      _navigationStack.end()
    );

    _pages.erase(it);
}

void UIPageManager::ShowPage(const UIPageID& id) {
    auto it = _pages.find(id);
    if (it == _pages.end()) {
        spdlog::warn("UIPageManager::ShowPage: unknown page '{}'", id);
        return;
    }
    UIPage* page = it->second.page.get();
    if (page->_document) {
        page->_document->Show();
        page->OnShow();
    }
}

void UIPageManager::HidePage(const UIPageID& id) {
    auto it = _pages.find(id);
    if (it == _pages.end()) return;

    UIPage* page = it->second.page.get();
    if (page->_document) {
        page->_document->Hide();
        page->OnHide();
    }
}

void UIPageManager::PushPage(const UIPageID& id) {
    if (!_navigationStack.empty()) {
        HidePage(_navigationStack.back());
    }
    _navigationStack.push_back(id);
    ShowPage(id);
}

void UIPageManager::PopPage() {
    if (_navigationStack.empty()) return;

    HidePage(_navigationStack.back());
    _navigationStack.pop_back();

    if (!_navigationStack.empty()) {
        ShowPage(_navigationStack.back());
    }
}

const UIPageID& UIPageManager::GetCurrentPage() const {
    static const UIPageID empty;
    if (_navigationStack.empty()) return empty;
    return _navigationStack.back();
}

void UIPageManager::Update(float dt) {
    for (auto& [id, entry] : _pages) {
        if (entry.page->IsVisible()) {
            entry.page->OnUpdate(dt);
        }
    }
}

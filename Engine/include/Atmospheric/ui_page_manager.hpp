#pragma once
#include "ui_page.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using UIPageID = std::string;

// UIPageManager owns all UIPage instances and drives their lifecycle.
//
// Two navigation models are supported:
//   Overlay  – ShowPage/HidePage for HUDs, subtitles, and other always-on elements
//   Stack    – PushPage/PopPage for menus that need back-navigation
//
// Both models can be used together: overlay pages remain visible independently
// of the navigation stack.
class UIPageManager {
public:
    static UIPageManager* Get();

    UIPageManager();
    ~UIPageManager();

    // Register a page, eagerly load its RmlUi document, and call OnAttach.
    // Returns a raw (non-owning) pointer to the concrete page type.
    template<typename T, typename... Args>
    T* AddPage(const UIPageID& id, const std::string& documentPath, Args&&... args) {
        static_assert(std::is_base_of<UIPage, T>::value, "T must derive from UIPage");
        auto page = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = page.get();
        _pages.emplace(id, PageEntry{ std::move(page), documentPath });
        _loadDocument(id);
        return ptr;
    }

    // Retrieve a previously registered page, cast to the concrete type.
    // Returns nullptr if the id is unknown or the cast fails.
    template<typename T>
    T* GetPage(const UIPageID& id) const {
        auto it = _pages.find(id);
        if (it == _pages.end()) return nullptr;
        return dynamic_cast<T*>(it->second.page.get());
    }

    // Unload the document and call OnDetach, then remove the page.
    void RemovePage(const UIPageID& id);

    // ---- Overlay model ----
    void ShowPage(const UIPageID& id);
    void HidePage(const UIPageID& id);

    // ---- Stack navigation model ----
    // Hide the current top, push and show the new page.
    void PushPage(const UIPageID& id);
    // Hide the current top and pop it; the page beneath becomes visible again.
    void PopPage();
    // Returns empty string when the stack is empty.
    const UIPageID& GetCurrentPage() const;

    // Call once per frame (driven by GameLayer).
    void Update(float dt);

private:
    struct PageEntry {
        std::unique_ptr<UIPage> page;
        std::string documentPath;
    };

    std::unordered_map<UIPageID, PageEntry> _pages;
    std::vector<UIPageID> _navigationStack;

    static UIPageManager* s_instance;

    void _loadDocument(const UIPageID& id);
};

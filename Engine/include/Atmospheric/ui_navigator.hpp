#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// UINavigator — page registry + menu navigation stack
//
// The UI operation layer ported from Vapor's PageSystem: pages register under
// an id, always-on pages are driven with Show/Hide, and menus flow through a
// push/pop stack (pushing hides the page below, popping restores it).
// UIPageComponent implements IUIPage; games register their pages after
// creating them:
//
//   enum : UINavigator::PageId { kHud = 1, kPauseMenu, kSettings };
//   UINavigator::Get()->Register(kPauseMenu, pausePage);
//   UINavigator::Get()->Push(kPauseMenu);
//
// PageIds are opaque uint32 constants DEFINED BY THE GAME — deliberately not
// an engine enum. (Vapor's engine-side PageID hardcoding game pages is a
// layering wart this port fixes rather than copies.)
//
// Owned by Application like the other services; Get() is the non-owning
// locator. Navigation calls the pages' Show/Hide, which UIPageComponent
// treats as visibility INTENT (see OnShow/OnHide there) — so animated
// transitions can happen underneath without the navigator knowing.
// ─────────────────────────────────────────────────────────────────────────────

// Minimal interface the navigator drives. UIPageComponent implements it;
// kept free of engine/RmlUi dependencies so the navigator (and its unit
// tests) build standalone.
class IUIPage {
public:
    virtual ~IUIPage() = default;
    virtual void Show() = 0;
    virtual void Hide() = 0;
};

class UINavigator {
public:
    using PageId = uint32_t;

    UINavigator();
    ~UINavigator();
    UINavigator(const UINavigator&) = delete;
    UINavigator& operator=(const UINavigator&) = delete;

    static UINavigator* Get() {
        return _instance;
    }

    // ── registry ─────────────────────────────────────────────────────────────
    void Register(PageId id, IUIPage* page);
    // Remove every entry pointing at `page` (registry and stack). Called by
    // UIPageComponent::OnDetach so a dying page can never be navigated to.
    void Unregister(IUIPage* page);
    IUIPage* Find(PageId id) const;

    // ── always-on pages (HUD-style, not part of the stack) ───────────────────
    void Show(PageId id);
    void Hide(PageId id);

    // ── menu stack ───────────────────────────────────────────────────────────
    void Push(PageId id);// hides the current top, shows the pushed page
    void Pop();          // hides the top, restores the page below (if any)
    void PopAll();       // hides everything on the stack

    bool IsTopOfStack(PageId id) const;
    bool IsStackEmpty() const {
        return _stack.empty();
    }
    size_t StackSize() const {
        return _stack.size();
    }

private:
    static UINavigator* _instance;

    std::unordered_map<PageId, IUIPage*> _pages;
    std::vector<PageId> _stack;
};

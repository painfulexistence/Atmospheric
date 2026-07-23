#include <catch2/catch_test_macros.hpp>
#include "ui_navigator.hpp"

// UINavigator drives IUIPage, so a plain mock exercises the whole registry /
// stack contract without RmlUi or the engine (the same MockPage approach
// Vapor's ui_system_test uses).

namespace {

    struct MockPage : IUIPage {
        bool visible = false;
        int showCount = 0;
        int hideCount = 0;

        void Show() override {
            visible = true;
            showCount++;
        }
        void Hide() override {
            visible = false;
            hideCount++;
        }
    };

    enum : UINavigator::PageId { kHud = 1, kMainMenu, kPauseMenu, kSettings };

}// namespace

TEST_CASE("UINavigator - registry and show/hide", "[ui]") {
    UINavigator nav;
    MockPage hud;
    nav.Register(kHud, &hud);

    CHECK(nav.Find(kHud) == &hud);
    CHECK(nav.Find(kMainMenu) == nullptr);

    nav.Show(kHud);
    CHECK(hud.visible);
    nav.Hide(kHud);
    CHECK(!hud.visible);

    SECTION("show/hide of an unknown id is a no-op") {
        nav.Show(kSettings);
        nav.Hide(kSettings);
        CHECK(hud.showCount == 1);
    }
}

TEST_CASE("UINavigator - menu stack", "[ui]") {
    UINavigator nav;
    MockPage menu, pause, settings;
    nav.Register(kMainMenu, &menu);
    nav.Register(kPauseMenu, &pause);
    nav.Register(kSettings, &settings);

    CHECK(nav.IsStackEmpty());

    SECTION("push shows the page") {
        nav.Push(kMainMenu);
        CHECK(menu.visible);
        CHECK(nav.StackSize() == 1);
        CHECK(nav.IsTopOfStack(kMainMenu));
    }

    SECTION("pushing a second page hides the first") {
        nav.Push(kMainMenu);
        nav.Push(kPauseMenu);
        CHECK(!menu.visible);
        CHECK(pause.visible);
        CHECK(nav.IsTopOfStack(kPauseMenu));
        CHECK(!nav.IsTopOfStack(kMainMenu));
    }

    SECTION("pop restores the page below") {
        nav.Push(kMainMenu);
        nav.Push(kPauseMenu);
        nav.Pop();
        CHECK(menu.visible);
        CHECK(!pause.visible);
        CHECK(nav.IsTopOfStack(kMainMenu));
    }

    SECTION("popAll hides everything") {
        nav.Push(kMainMenu);
        nav.Push(kPauseMenu);
        nav.PopAll();
        CHECK(!menu.visible);
        CHECK(!pause.visible);
        CHECK(nav.IsStackEmpty());
    }

    SECTION("pushing an unregistered id leaves the stack untouched") {
        nav.Push(static_cast<UINavigator::PageId>(99));
        CHECK(nav.IsStackEmpty());
    }
}

TEST_CASE("UINavigator - unregister scrubs the registry and the stack", "[ui]") {
    UINavigator nav;
    MockPage menu, pause;
    nav.Register(kMainMenu, &menu);
    nav.Register(kPauseMenu, &pause);

    nav.Push(kMainMenu);
    nav.Push(kPauseMenu);

    // The top page dies (component detached): it must vanish from the stack
    // and the page below is restored as the new top.
    nav.Unregister(&pause);
    CHECK(nav.Find(kPauseMenu) == nullptr);
    CHECK(nav.StackSize() == 1);
    CHECK(nav.IsTopOfStack(kMainMenu));
    CHECK(menu.visible);

    nav.Unregister(&menu);
    CHECK(nav.IsStackEmpty());
}

TEST_CASE("UINavigator - locator", "[ui]") {
    CHECK(UINavigator::Get() == nullptr);
    {
        UINavigator nav;
        CHECK(UINavigator::Get() == &nav);
    }
    CHECK(UINavigator::Get() == nullptr);
}

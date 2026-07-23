#include "ui_navigator.hpp"
#include <algorithm>

UINavigator* UINavigator::_instance = nullptr;

UINavigator::UINavigator() {
    if (!_instance) _instance = this;
}

UINavigator::~UINavigator() {
    if (_instance == this) _instance = nullptr;
}

void UINavigator::Register(PageId id, IUIPage* page) {
    if (!page) return;
    _pages[id] = page;
}

void UINavigator::Unregister(IUIPage* page) {
    if (!page) return;
    for (auto it = _pages.begin(); it != _pages.end();) {
        if (it->second == page) {
            const PageId id = it->first;
            _stack.erase(std::remove(_stack.begin(), _stack.end(), id), _stack.end());
            it = _pages.erase(it);
        } else {
            ++it;
        }
    }
    // If the removed page was covering another menu, restore the new top.
    if (!_stack.empty()) {
        if (IUIPage* top = Find(_stack.back())) top->Show();
    }
}

IUIPage* UINavigator::Find(PageId id) const {
    auto it = _pages.find(id);
    return it != _pages.end() ? it->second : nullptr;
}

void UINavigator::Show(PageId id) {
    if (IUIPage* page = Find(id)) page->Show();
}

void UINavigator::Hide(PageId id) {
    if (IUIPage* page = Find(id)) page->Hide();
}

void UINavigator::Push(PageId id) {
    IUIPage* page = Find(id);
    if (!page) return;
    if (!_stack.empty()) {
        if (IUIPage* top = Find(_stack.back())) top->Hide();
    }
    _stack.push_back(id);
    page->Show();
}

void UINavigator::Pop() {
    if (_stack.empty()) return;
    if (IUIPage* top = Find(_stack.back())) top->Hide();
    _stack.pop_back();
    if (!_stack.empty()) {
        if (IUIPage* top = Find(_stack.back())) top->Show();
    }
}

void UINavigator::PopAll() {
    for (PageId id : _stack) {
        if (IUIPage* page = Find(id)) page->Hide();
    }
    _stack.clear();
}

bool UINavigator::IsTopOfStack(PageId id) const {
    return !_stack.empty() && _stack.back() == id;
}

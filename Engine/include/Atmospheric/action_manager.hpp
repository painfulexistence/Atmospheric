#pragma once

#include "action.hpp"
#include "component.hpp"
#include <memory>
#include <vector>

class ActionManager : public Component {
public:
    ActionManager(GameObject* gameObject);
    ~ActionManager();

    std::string GetName() const override {
        return "ActionManager";
    }

    void OnTick(float dt) override;

    // Takes ownership (callers pass `new T(...)`; used by the Lua bindings).
    void RunAction(Action* action);
    void RunAction(std::unique_ptr<Action> action);
    // Safe to call from inside a running action: removal is deferred to the
    // end of the current OnTick pass.
    void StopAllActions();

private:
    std::vector<std::unique_ptr<Action>> _actions;
    std::vector<std::unique_ptr<Action>> _actionsToAdd;
    bool _isUpdating = false;
    bool _stopRequested = false;
};

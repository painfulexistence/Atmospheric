#pragma once

#include "action.hpp"
#include "component.hpp"
#include <vector>

class ActionManager : public Component {
public:
    ActionManager(GameObject* gameObject);
    ~ActionManager();

    std::string GetName() const override {
        return "ActionManager";
    }

    void OnTick(float dt) override;

    void RunAction(Action* action);
    void StopAllActions();

private:
    std::vector<Action*> _actions;
    std::vector<Action*> _actionsToAdd;
    bool _isUpdating = false;
};

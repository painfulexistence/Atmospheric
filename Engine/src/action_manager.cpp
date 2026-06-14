#include "action_manager.hpp"
#include "game_object.hpp"

ActionManager::ActionManager(GameObject* gameObject) {
    this->gameObject = gameObject;
}

ActionManager::~ActionManager() {
    StopAllActions();
}

void ActionManager::OnTick(float dt) {
    _isUpdating = true;
    for (auto it = _actions.begin(); it != _actions.end();) {
        Action* action = *it;
        action->Step(dt);

        if (action->IsDone()) {
            delete action;
            it = _actions.erase(it);
        } else {
            ++it;
        }
    }
    _isUpdating = false;

    // Add pending actions
    if (!_actionsToAdd.empty()) {
        for (auto action : _actionsToAdd) {
            _actions.push_back(action);
            action->StartWithTarget(gameObject);
        }
        _actionsToAdd.clear();
    }
}

void ActionManager::RunAction(Action* action) {
    if (_isUpdating) {
        _actionsToAdd.push_back(action);
    } else {
        _actions.push_back(action);
        action->StartWithTarget(gameObject);
    }
}

void ActionManager::StopAllActions() {
    for (auto action : _actions) {
        delete action;
    }
    _actions.clear();
    for (auto action : _actionsToAdd) {
        delete action;
    }
    _actionsToAdd.clear();
}

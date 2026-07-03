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
        (*it)->Step(dt);
        if (_stopRequested) break;// an action stopped us; the list may not be mutated mid-walk

        if ((*it)->IsDone()) {
            it = _actions.erase(it);
        } else {
            ++it;
        }
    }
    _isUpdating = false;

    if (_stopRequested) {
        _stopRequested = false;
        _actions.clear();
        _actionsToAdd.clear();
        return;
    }

    // Add pending actions
    for (auto& action : _actionsToAdd) {
        auto* raw = action.get();
        _actions.push_back(std::move(action));
        raw->StartWithTarget(gameObject);
    }
    _actionsToAdd.clear();
}

void ActionManager::RunAction(Action* action) {
    RunAction(std::unique_ptr<Action>(action));
}

void ActionManager::RunAction(std::unique_ptr<Action> action) {
    if (!action) return;
    if (_isUpdating) {
        _actionsToAdd.push_back(std::move(action));
    } else {
        auto* raw = action.get();
        _actions.push_back(std::move(action));
        raw->StartWithTarget(gameObject);
    }
}

void ActionManager::StopAllActions() {
    if (_isUpdating) {
        _stopRequested = true;// defer: OnTick clears everything after the walk
        return;
    }
    _actions.clear();
    _actionsToAdd.clear();
}

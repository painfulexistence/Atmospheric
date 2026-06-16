#pragma once
#include <string>

class Layer {
public:
    Layer(const std::string& name = "Layer") : _debugName(name) {
    }
    virtual ~Layer() = default;

    virtual void OnAttach() {
    }
    virtual void OnDetach() {
    }
    virtual void OnUpdate(float dt) {
    }
    virtual void OnRender(float dt) {
    }

    inline const std::string& GetName() const {
        return _debugName;
    }

protected:
    std::string _debugName;
};

#pragma once
#include "component.hpp"
#include "globals.hpp"

class BatchRenderer2D;

// Base class for any 2D visual element drawn by the CanvasPass
class CanvasDrawable : public Component {
public:
    CanvasDrawable(GameObject* gameObject) {
        this->gameObject = gameObject;
    }
    virtual ~CanvasDrawable() = default;

    virtual void Draw(BatchRenderer2D* renderer) = 0;

    virtual CanvasLayer GetLayer() const = 0;

    virtual int GetZOrder() const { return 0; }
};

#pragma once
#include "globals.hpp"
#include "bullet_linear_math.hpp"

class PhysicsDebugDrawer : public btIDebugDraw {
private:
    int _mode = DebugDrawModes::DBG_DrawWireframe;

public:
    PhysicsDebugDrawer() = default;
    ~PhysicsDebugDrawer() = default;

    void setDebugMode(int mode) override {
        _mode = mode;
    };
    int getDebugMode() const override {
        return _mode;
    };

    void reportErrorWarning(const char*) override;

    void drawLine(const btVector3&, const btVector3&, const btVector3&) override;
    void drawContactPoint(const btVector3&, const btVector3&, btScalar, int, const btVector3&) override;
    void draw3dText(const btVector3&, const char*) override;
};
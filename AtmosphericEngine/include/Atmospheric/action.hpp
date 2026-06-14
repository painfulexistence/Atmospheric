#pragma once

#include "component.hpp"
#include "game_object.hpp"
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GameObject;
class SpriteComponent;

// Forward declaration of AnimationClip (we'll keep it in animator_2d.hpp or move it later)
struct AnimationClip;

// Easing types (compatible with Cocos Studio CSB format)
enum class EasingType {
    Linear = 0,
    SineIn = 1,
    SineOut = 2,
    SineInOut = 3,
    QuadIn = 4,
    QuadOut = 5,
    QuadInOut = 6,
    CubicIn = 7,
    CubicOut = 8,
    CubicInOut = 9,
    QuartIn = 10,
    QuartOut = 11,
    QuartInOut = 12,
    QuintIn = 13,
    QuintOut = 14,
    QuintInOut = 15,
    ExpoIn = 16,
    ExpoOut = 17,
    ExpoInOut = 18,
    CircIn = 19,
    CircOut = 20,
    CircInOut = 21,
    BackIn = 22,
    BackOut = 23,
    BackInOut = 24,
    ElasticIn = 25,
    ElasticOut = 26,
    ElasticInOut = 27,
    BounceIn = 28,
    BounceOut = 29,
    BounceInOut = 30,
};

// Apply easing function to t (0.0 to 1.0)
float ApplyEasing(float t, EasingType type);

class Action {
public:
    virtual ~Action() = default;

    virtual bool IsDone() const {
        return true;
    }
    virtual void Step(float dt) {
    }
    virtual void StartWithTarget(GameObject* target) {
        _target = target;
    }
    virtual void Stop() {
        _target = nullptr;
    }

protected:
    GameObject* _target = nullptr;
};

class FiniteTimeAction : public Action {
public:
    FiniteTimeAction(float duration) : _duration(duration) {
    }
    float GetDuration() const {
        return _duration;
    }
    void SetDuration(float duration) {
        _duration = duration;
    }

protected:
    float _duration;
};

class ActionInterval : public FiniteTimeAction {
public:
    ActionInterval(float duration, EasingType easing = EasingType::Linear);

    bool IsDone() const override;
    void Step(float dt) override;
    void StartWithTarget(GameObject* target) override;

    void SetEasing(EasingType easing) {
        _easing = easing;
    }
    EasingType GetEasing() const {
        return _easing;
    }

    virtual void Update(float t) = 0;// t is 0.0 to 1.0 (after easing applied)

protected:
    float _elapsed;
    bool _firstTick;
    EasingType _easing;
};

// --- Concrete Actions ---

class MoveTo : public ActionInterval {
public:
    MoveTo(float duration, const glm::vec3& position);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec3 _endPosition;
    glm::vec3 _startPosition;
    glm::vec3 _delta;
};

class MoveBy : public ActionInterval {
public:
    MoveBy(float duration, const glm::vec3& deltaPosition);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec3 _delta;
    glm::vec3 _startPosition;
    glm::vec3 _previousPosition;
};

class RotateTo : public ActionInterval {
public:
    RotateTo(float duration, const glm::vec3& rotation);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec3 _dstRotation;
    glm::vec3 _startRotation;
    glm::vec3 _diffRotation;
};

class RotateBy : public ActionInterval {
public:
    RotateBy(float duration, const glm::vec3& deltaRotation);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec3 _delta;
    glm::vec3 _startRotation;
};

class ScaleTo : public ActionInterval {
public:
    ScaleTo(float duration, const glm::vec3& scale);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec3 _endScale;
    glm::vec3 _startScale;
    glm::vec3 _delta;
};

class ColorTo : public ActionInterval {
public:
    ColorTo(float duration, const glm::vec4& color);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    glm::vec4 _endColor;
    glm::vec4 _startColor;
    glm::vec4 _delta;
};

class FadeTo : public ActionInterval {
public:
    FadeTo(float duration, float alpha);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    float _endAlpha;
    float _startAlpha;
};

class Sequence : public ActionInterval {
public:
    Sequence(const std::vector<FiniteTimeAction*>& actions);
    ~Sequence();

    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;
    void Step(float dt) override;
    bool IsDone() const override;

private:
    std::vector<FiniteTimeAction*> _actions;
    int _currentActionIndex;
    FiniteTimeAction* _currentAction;
};

class CallFunc : public FiniteTimeAction {
public:
    CallFunc(std::function<void()> func);
    void StartWithTarget(GameObject* target) override;
    void Step(float dt) override;
    bool IsDone() const override {
        return _done;
    }

private:
    std::function<void()> _function;
    bool _done = false;
};

class RepeatForever : public Action {
public:
    RepeatForever(ActionInterval* action);
    ~RepeatForever();

    void StartWithTarget(GameObject* target) override;
    void Step(float dt) override;
    bool IsDone() const override {
        return false;
    }

private:
    ActionInterval* _innerAction;
};

// Animation Action
struct AnimationClip;// Forward decl
class Animate : public ActionInterval {
public:
    Animate(const AnimationClip& clip);
    void StartWithTarget(GameObject* target) override;
    void Update(float t) override;

private:
    const AnimationClip* _clip;// We store a copy or pointer? Pointer is unsafe if clip is temporary.
    // Actually, AnimationClip is usually a resource. Let's store a copy for safety or assume resource manager.
    // For now, let's assume we pass a copy to the constructor but store it internally.
    // Wait, AnimationClip struct was defined in animator_2d.hpp. We need to move it or include it.
    // Let's include animator_2d.hpp for now, or redefine it here if we remove Animator2D.

    // Let's redefine AnimationFrame and AnimationClip here or in a common header.
    // But since we are deprecating Animator2D, we should probably move the structs to a new header or keep them in
    // animator_2d.hpp and include it. I'll include "animator_2d.hpp" for the structs.

    std::vector<float> _splitTimes;
    int _currentFrame;
    SpriteComponent* _sprite = nullptr;
};

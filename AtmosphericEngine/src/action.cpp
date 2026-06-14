#include "action.hpp"
#include "animator_2d.hpp"// For AnimationClip definition
#include "sprite_component.hpp"
#include "text_component.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Easing Functions ---

static float EaseInSine(float t) {
    return 1.0f - std::cos((t * M_PI) / 2.0f);
}
static float EaseOutSine(float t) {
    return std::sin((t * M_PI) / 2.0f);
}
static float EaseInOutSine(float t) {
    return -(std::cos(M_PI * t) - 1.0f) / 2.0f;
}

static float EaseInQuad(float t) {
    return t * t;
}
static float EaseOutQuad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}
static float EaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

static float EaseInCubic(float t) {
    return t * t * t;
}
static float EaseOutCubic(float t) {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}
static float EaseInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

static float EaseInQuart(float t) {
    return t * t * t * t;
}
static float EaseOutQuart(float t) {
    return 1.0f - std::pow(1.0f - t, 4.0f);
}
static float EaseInOutQuart(float t) {
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

static float EaseInQuint(float t) {
    return t * t * t * t * t;
}
static float EaseOutQuint(float t) {
    return 1.0f - std::pow(1.0f - t, 5.0f);
}
static float EaseInOutQuint(float t) {
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

static float EaseInExpo(float t) {
    return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
}
static float EaseOutExpo(float t) {
    return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
static float EaseInOutExpo(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

static float EaseInCirc(float t) {
    return 1.0f - std::sqrt(1.0f - t * t);
}
static float EaseOutCirc(float t) {
    return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
}
static float EaseInOutCirc(float t) {
    return t < 0.5f ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) / 2.0f
                    : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

static float EaseInBack(float t) {
    const float c1 = 1.70158f;
    return (c1 + 1.0f) * t * t * t - c1 * t * t;
}
static float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    return 1.0f + (c1 + 1.0f) * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}
static float EaseInOutBack(float t) {
    const float c2 = 1.70158f * 1.525f;
    return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                    : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
}

static float EaseOutBounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) return n1 * (t -= 1.5f / d1) * t + 0.75f;
    if (t < 2.5f / d1) return n1 * (t -= 2.25f / d1) * t + 0.9375f;
    return n1 * (t -= 2.625f / d1) * t + 0.984375f;
}
static float EaseInBounce(float t) {
    return 1.0f - EaseOutBounce(1.0f - t);
}
static float EaseInOutBounce(float t) {
    return t < 0.5f ? (1.0f - EaseOutBounce(1.0f - 2.0f * t)) / 2.0f : (1.0f + EaseOutBounce(2.0f * t - 1.0f)) / 2.0f;
}

static float EaseInElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float c4 = (2.0f * M_PI) / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
static float EaseOutElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float c4 = (2.0f * M_PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
static float EaseInOutElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float c5 = (2.0f * M_PI) / 4.5f;
    return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                    : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
}

float ApplyEasing(float t, EasingType type) {
    switch (type) {
    case EasingType::Linear:
        return t;
    case EasingType::SineIn:
        return EaseInSine(t);
    case EasingType::SineOut:
        return EaseOutSine(t);
    case EasingType::SineInOut:
        return EaseInOutSine(t);
    case EasingType::QuadIn:
        return EaseInQuad(t);
    case EasingType::QuadOut:
        return EaseOutQuad(t);
    case EasingType::QuadInOut:
        return EaseInOutQuad(t);
    case EasingType::CubicIn:
        return EaseInCubic(t);
    case EasingType::CubicOut:
        return EaseOutCubic(t);
    case EasingType::CubicInOut:
        return EaseInOutCubic(t);
    case EasingType::QuartIn:
        return EaseInQuart(t);
    case EasingType::QuartOut:
        return EaseOutQuart(t);
    case EasingType::QuartInOut:
        return EaseInOutQuart(t);
    case EasingType::QuintIn:
        return EaseInQuint(t);
    case EasingType::QuintOut:
        return EaseOutQuint(t);
    case EasingType::QuintInOut:
        return EaseInOutQuint(t);
    case EasingType::ExpoIn:
        return EaseInExpo(t);
    case EasingType::ExpoOut:
        return EaseOutExpo(t);
    case EasingType::ExpoInOut:
        return EaseInOutExpo(t);
    case EasingType::CircIn:
        return EaseInCirc(t);
    case EasingType::CircOut:
        return EaseOutCirc(t);
    case EasingType::CircInOut:
        return EaseInOutCirc(t);
    case EasingType::BackIn:
        return EaseInBack(t);
    case EasingType::BackOut:
        return EaseOutBack(t);
    case EasingType::BackInOut:
        return EaseInOutBack(t);
    case EasingType::ElasticIn:
        return EaseInElastic(t);
    case EasingType::ElasticOut:
        return EaseOutElastic(t);
    case EasingType::ElasticInOut:
        return EaseInOutElastic(t);
    case EasingType::BounceIn:
        return EaseInBounce(t);
    case EasingType::BounceOut:
        return EaseOutBounce(t);
    case EasingType::BounceInOut:
        return EaseInOutBounce(t);
    default:
        return t;
    }
}

// --- ActionInterval ---

ActionInterval::ActionInterval(float duration, EasingType easing)
  : FiniteTimeAction(duration), _elapsed(0), _firstTick(true), _easing(easing) {
    if (_duration == 0) _duration = FLT_EPSILON;
}

bool ActionInterval::IsDone() const {
    return _elapsed >= _duration;
}

void ActionInterval::Step(float dt) {
    if (_firstTick) {
        _firstTick = false;
        _elapsed = 0;
    } else {
        _elapsed += dt;
    }

    float t = std::min(1.0f, _elapsed / _duration);
    float easedT = ApplyEasing(t, _easing);
    Update(easedT);
}

void ActionInterval::StartWithTarget(GameObject* target) {
    Action::StartWithTarget(target);
    _elapsed = 0;
    _firstTick = true;
}

// --- MoveTo ---

MoveTo::MoveTo(float duration, const glm::vec3& position) : ActionInterval(duration), _endPosition(position) {
}

void MoveTo::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _startPosition = target->GetPosition();
    _delta = _endPosition - _startPosition;
}

void MoveTo::Update(float t) {
    if (_target) {
        _target->SetPosition(_startPosition + _delta * t);
    }
}

// --- MoveBy ---

MoveBy::MoveBy(float duration, const glm::vec3& deltaPosition) : ActionInterval(duration), _delta(deltaPosition) {
}

void MoveBy::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _startPosition = target->GetPosition();
    _previousPosition = _startPosition;
}

void MoveBy::Update(float t) {
    if (_target) {
        glm::vec3 currentPos = _target->GetPosition();
        glm::vec3 diff = currentPos - _previousPosition;
        _startPosition = _startPosition + diff;

        glm::vec3 newPos = _startPosition + _delta * t;
        _target->SetPosition(newPos);
        _previousPosition = newPos;
    }
}

// --- RotateTo ---

RotateTo::RotateTo(float duration, const glm::vec3& rotation) : ActionInterval(duration), _dstRotation(rotation) {
}

void RotateTo::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _startRotation = target->GetRotation();
    _diffRotation = _dstRotation - _startRotation;
}

void RotateTo::Update(float t) {
    if (_target) {
        _target->SetRotation(_startRotation + _diffRotation * t);
    }
}

// --- RotateBy ---

RotateBy::RotateBy(float duration, const glm::vec3& deltaRotation) : ActionInterval(duration), _delta(deltaRotation) {
}

void RotateBy::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _startRotation = target->GetRotation();
}

void RotateBy::Update(float t) {
    // Simple implementation, might drift if modified externally
    if (_target) {
        _target->SetRotation(_startRotation + _delta * t);
    }
}

// --- ScaleTo ---

ScaleTo::ScaleTo(float duration, const glm::vec3& scale) : ActionInterval(duration), _endScale(scale) {
}

void ScaleTo::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _startScale = target->GetScale();
    _delta = _endScale - _startScale;
}

void ScaleTo::Update(float t) {
    if (_target) {
        _target->SetScale(_startScale + _delta * t);
    }
}

// --- Sequence ---

Sequence::Sequence(const std::vector<FiniteTimeAction*>& actions) : ActionInterval(0), _actions(actions) {
    float totalDuration = 0;
    for (auto action : _actions) {
        totalDuration += action->GetDuration();
    }
    _duration = totalDuration;
}

Sequence::~Sequence() {
    for (auto action : _actions) {
        delete action;
    }
}

void Sequence::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _currentActionIndex = 0;
    if (!_actions.empty()) {
        _currentAction = _actions[0];
        _currentAction->StartWithTarget(target);
    } else {
        _currentAction = nullptr;
    }
}

void Sequence::Step(float dt) {
    if (_currentAction) {
        _currentAction->Step(dt);
        if (_currentAction->IsDone()) {
            _currentActionIndex++;
            if (_currentActionIndex < _actions.size()) {
                _currentAction = _actions[_currentActionIndex];
                _currentAction->StartWithTarget(_target);
            } else {
                _currentAction = nullptr;
            }
        }
    }
}

void Sequence::Update(float t) {
    // Not used for Sequence
}

bool Sequence::IsDone() const {
    // Sequence is done when all actions have completed
    return _currentActionIndex >= static_cast<int>(_actions.size());
}

// --- CallFunc ---

CallFunc::CallFunc(std::function<void()> func) : FiniteTimeAction(0), _function(func) {
}

void CallFunc::StartWithTarget(GameObject* target) {
    Action::StartWithTarget(target);
    if (_function) _function();
    _done = true;
}

void CallFunc::Step(float dt) {
    // Executed in StartWithTarget
}

// --- RepeatForever ---

RepeatForever::RepeatForever(ActionInterval* action) : _innerAction(action) {
}

RepeatForever::~RepeatForever() {
    delete _innerAction;
}

void RepeatForever::StartWithTarget(GameObject* target) {
    Action::StartWithTarget(target);
    _innerAction->StartWithTarget(target);
}

void RepeatForever::Step(float dt) {
    _innerAction->Step(dt);
    if (_innerAction->IsDone()) {
        _innerAction->StartWithTarget(_target);
    }
}

// --- Animate ---

Animate::Animate(const AnimationClip& clip) : ActionInterval(0), _clip(new AnimationClip(clip)) {
    float totalDuration = 0;
    for (const auto& frame : _clip->frames) {
        totalDuration += frame.duration;
        _splitTimes.push_back(totalDuration);
    }
    _duration = totalDuration;
    // Normalize split times
    if (_duration > 0) {
        for (float& t : _splitTimes) {
            t /= _duration;
        }
    }
}

void Animate::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    _sprite = target->GetComponent<SpriteComponent>();
    _currentFrame = 0;
}

void Animate::Update(float t) {
    if (!_sprite || !_clip) return;

    // Find frame based on t
    int frameIndex = 0;
    for (size_t i = 0; i < _splitTimes.size(); ++i) {
        if (t <= _splitTimes[i]) {
            frameIndex = i;
            break;
        }
    }
    if (t >= 1.0f) frameIndex = (int)_splitTimes.size() - 1;

    if (frameIndex != _currentFrame) {
        _currentFrame = frameIndex;
        const auto& frame = _clip->frames[_currentFrame];
        _sprite->SetUVs(frame.uvMin, frame.uvMax);
    }
}

// --- ColorTo ---

ColorTo::ColorTo(float duration, const glm::vec4& color) : ActionInterval(duration), _endColor(color) {
}

void ColorTo::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    // Try SpriteComponent first, then TextComponent
    if (auto* sprite = target->GetComponent<SpriteComponent>()) {
        _startColor = sprite->GetColor();
    } else if (auto* text = target->GetComponent<TextComponent>()) {
        _startColor = text->GetColor();
    } else {
        _startColor = glm::vec4(1.0f);
    }
    _delta = _endColor - _startColor;
}

void ColorTo::Update(float t) {
    if (!_target) return;
    glm::vec4 newColor = _startColor + _delta * t;
    if (auto* sprite = _target->GetComponent<SpriteComponent>()) {
        sprite->SetColor(newColor);
    } else if (auto* text = _target->GetComponent<TextComponent>()) {
        text->SetColor(newColor);
    }
}

// --- FadeTo ---

FadeTo::FadeTo(float duration, float alpha) : ActionInterval(duration), _endAlpha(alpha) {
}

void FadeTo::StartWithTarget(GameObject* target) {
    ActionInterval::StartWithTarget(target);
    if (auto* sprite = target->GetComponent<SpriteComponent>()) {
        _startAlpha = sprite->GetColor().a;
    } else if (auto* text = target->GetComponent<TextComponent>()) {
        _startAlpha = text->GetColor().a;
    } else {
        _startAlpha = 1.0f;
    }
}

void FadeTo::Update(float t) {
    if (!_target) return;
    float newAlpha = _startAlpha + (_endAlpha - _startAlpha) * t;
    if (auto* sprite = _target->GetComponent<SpriteComponent>()) {
        glm::vec4 color = sprite->GetColor();
        color.a = newAlpha;
        sprite->SetColor(color);
    } else if (auto* text = _target->GetComponent<TextComponent>()) {
        glm::vec4 color = text->GetColor();
        color.a = newAlpha;
        text->SetColor(color);
    }
}

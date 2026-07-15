#include "easing.hpp"
#include <cmath>
#include <numbers>

// Easing curve implementations, shared by the whole animation stack. Extracted
// verbatim from the former action.cpp so the Action hierarchy could be removed
// without losing the curves (ActionTrack / tween sampling still needs them).

inline constexpr float PI = std::numbers::pi_v<float>;

static float EaseInSine(float t) {
    return 1.0f - std::cos((t * PI) / 2.0f);
}
static float EaseOutSine(float t) {
    return std::sin((t * PI) / 2.0f);
}
static float EaseInOutSine(float t) {
    return -(std::cos(PI * t) - 1.0f) / 2.0f;
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
    const float c4 = (2.0f * PI) / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
static float EaseOutElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float c4 = (2.0f * PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
static float EaseInOutElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    const float c5 = (2.0f * PI) / 4.5f;
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

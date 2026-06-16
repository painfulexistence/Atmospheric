#pragma once
#include "globals.hpp"
#include <cassert>

// Catmull-Rom spline parameterized by t in [0, 1].
// Works with any GLM vector type (vec2, vec3, vec4, float).
// Quaternion slerp is not handled — interpolate orientation separately.
//
// Usage:
//   Spline<glm::vec3> path;
//   path.AddPoint({0, 0, 100});
//   path.AddPoint({0, 0, 50});
//   path.AddPoint({0, 10, 0});
//   glm::vec3 pos = path.Sample(scrollT);          // position
//   glm::vec3 dir = glm::normalize(path.SampleTangent(scrollT));  // forward
template<typename T>
class Spline {
public:
    // When true, the spline loops back from the last point to the first.
    bool loop = false;

    void AddPoint(const T& point) {
        _points.push_back(point);
    }

    void SetPoints(const std::vector<T>& points) {
        _points = points;
    }

    const std::vector<T>& GetPoints() const {
        return _points;
    }

    void Clear() {
        _points.clear();
    }

    size_t PointCount() const {
        return _points.size();
    }

    // Returns the interpolated position at t in [0, 1].
    // t is distributed uniformly across segments — each consecutive
    // pair of control points forms one segment of equal t-width.
    T Sample(float t) const {
        assert(_points.size() >= 2 && "Spline needs at least 2 points");
        int seg;
        float lt;
        _Segment(t, seg, lt);
        return _CatmullRom(
            _Point(seg - 1), _Point(seg),
            _Point(seg + 1), _Point(seg + 2), lt);
    }

    // Returns the first derivative (tangent) at t in [0, 1].
    // Not normalized — call glm::normalize() if you need a unit direction.
    T SampleTangent(float t) const {
        assert(_points.size() >= 2 && "Spline needs at least 2 points");
        int seg;
        float lt;
        _Segment(t, seg, lt);
        return _CatmullRomDerivative(
            _Point(seg - 1), _Point(seg),
            _Point(seg + 1), _Point(seg + 2), lt);
    }

private:
    std::vector<T> _points;

    void _Segment(float t, int& seg, float& localT) const {
        t = glm::clamp(t, 0.0f, 1.0f);
        int n = static_cast<int>(_points.size());
        int numSegments = loop ? n : n - 1;
        float scaled = t * static_cast<float>(numSegments);
        seg = static_cast<int>(scaled);
        if (seg >= numSegments) seg = numSegments - 1;
        localT = scaled - static_cast<float>(seg);
    }

    T _Point(int i) const {
        int n = static_cast<int>(_points.size());
        if (loop)
            return _points[((i % n) + n) % n];
        return _points[glm::clamp(i, 0, n - 1)];
    }

    static T _CatmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
        float t2 = t * t;
        float t3 = t2 * t;
        return 0.5f * (
            p1 * 2.0f
            + (p2 - p0) * t
            + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
            + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3
        );
    }

    // Analytic derivative of _CatmullRom with respect to t.
    static T _CatmullRomDerivative(const T& p0, const T& p1, const T& p2, const T& p3, float t) {
        float t2 = t * t;
        return 0.5f * (
            (p2 - p0)
            + (p0 * 4.0f - p1 * 10.0f + p2 * 8.0f - p3 * 2.0f) * t
            + (p0 * (-3.0f) + p1 * 9.0f - p2 * 9.0f + p3 * 3.0f) * t2
        );
    }
};

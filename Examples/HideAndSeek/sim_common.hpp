#pragma once
#include <algorithm>
#include <cmath>

// Movement/visibility rules shared by the server (authoritative) and the
// client (local prediction of its own entity). Sharing this function is what
// lets the client's prediction usually agree with the server without any
// special-casing — it's just calling the same math both places.
//
// No physics engine involvement (Bullet/Box2D): this is a client-server
// game, not peer-symmetric lockstep, so there is no requirement for
// bit-identical cross-machine simulation — the server is the single source
// of truth and corrects any client that drifts. Plain kinematics is enough
// and sidesteps determinism concerns entirely.
namespace sim {

    constexpr float kArenaW = 800.0f;
    constexpr float kArenaH = 600.0f;
    constexpr float kMaxSpeed = 220.0f;// units/sec
    constexpr float kSeekerVisionRadius = 180.0f;
    constexpr float kTagDistance = 24.0f;
    constexpr float kTickRateHz = 30.0f;
    constexpr float kTickDt = 1.0f / kTickRateHz;

    struct Vec2 {
        float x = 0.0f, y = 0.0f;
    };

    // Advances a position by one tick given a (possibly client-reported,
    // therefore untrusted) direction. Speed and dt are fixed constants, not
    // taken from the caller, so a modified client sending an inflated
    // direction magnitude gains nothing — Step() clamps the direction to
    // unit length before scaling.
    inline Vec2 Step(Vec2 pos, float dx, float dy) {
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
            dx /= len;
            dy /= len;
        }
        pos.x = std::clamp(pos.x + dx * kMaxSpeed * kTickDt, 0.0f, kArenaW);
        pos.y = std::clamp(pos.y + dy * kMaxSpeed * kTickDt, 0.0f, kArenaH);
        return pos;
    }

    inline bool SeekerCanSeeHider(Vec2 seeker, Vec2 hider) {
        float ddx = seeker.x - hider.x;
        float ddy = seeker.y - hider.y;
        return (ddx * ddx + ddy * ddy) <= (kSeekerVisionRadius * kSeekerVisionRadius);
    }

    inline bool IsTagged(Vec2 seeker, Vec2 hider) {
        float ddx = seeker.x - hider.x;
        float ddy = seeker.y - hider.y;
        return (ddx * ddx + ddy * ddy) <= (kTagDistance * kTagDistance);
    }

}// namespace sim

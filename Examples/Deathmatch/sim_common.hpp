#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Movement / weapon / world geometry shared by the server (authoritative) and
// the client (local prediction of its own movement, cosmetic prediction of
// its own rockets). Sharing this math is what lets the client's prediction
// agree with the server exactly — reconciliation here is textbook
// rewind-replay (store inputs, on correction reset to the server's
// authoritative state and replay stored inputs forward), not HideAndSeek's
// error-smoothing, so "agree exactly" has to be literally true or the
// prediction visibly fights the server.
//
// No physics engine (Bullet/Box2D): server-authoritative, single simulator,
// so no cross-machine bit-determinism requirement — plain kinematics is
// enough and the server corrects any drift.
namespace sim {

    constexpr float kArenaW = 800.0f;
    constexpr float kArenaH = 600.0f;

    constexpr float kTickRateHz = 60.0f;
    constexpr float kTickDt = 1.0f / kTickRateHz;

    constexpr float kMoveSpeed = 220.0f;// units/sec
    constexpr float kPlayerRadius = 14.0f;

    // A single occluder wall (a vertical bar in the middle with gaps top and
    // bottom). This is what makes the "killed behind cover" downside of
    // favor-the-shooter lag compensation observable: duck behind this and a
    // favor-shooter rocket can still hit the you-of-a-moment-ago out in the open.
    constexpr float kWallX = 380.0f;
    constexpr float kWallY = 200.0f;
    constexpr float kWallW = 40.0f;
    constexpr float kWallH = 200.0f;

    constexpr float kRocketSpeed = 600.0f;// units/sec
    constexpr float kRocketRadius = 8.0f;

    constexpr int kMaxHealth = 100;
    constexpr int kRailDamage = 50;// 2 hits to down
    constexpr int kRocketDamage = 50;
    constexpr float kRespawnDelay = 1.5f;// seconds

    struct Vec2 {
        float x = 0.0f, y = 0.0f;
    };

    inline float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Pushes a circle out of the wall AABB if it overlaps. Shared by client
    // prediction and server authority so a player can never tunnel through the
    // wall on one side but not the other.
    inline Vec2 ResolveWall(Vec2 p, float r) {
        const float minx = kWallX, miny = kWallY, maxx = kWallX + kWallW, maxy = kWallY + kWallH;
        const float cx = Clamp(p.x, minx, maxx);
        const float cy = Clamp(p.y, miny, maxy);
        const float dx = p.x - cx, dy = p.y - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 > r * r) return p;// no overlap
        if (d2 > 1e-6f) {
            const float d = std::sqrt(d2);
            const float push = r - d;
            p.x += dx / d * push;
            p.y += dy / d * push;
        } else {
            // Center inside the AABB — push out along the least-penetration face.
            const float left = p.x - minx, right = maxx - p.x, top = p.y - miny, bot = maxy - p.y;
            const float m = std::min(std::min(left, right), std::min(top, bot));
            if (m == left)
                p.x = minx - r;
            else if (m == right)
                p.x = maxx + r;
            else if (m == top)
                p.y = miny - r;
            else
                p.y = maxy + r;
        }
        return p;
    }

    // Advances a player position by one tick given a (possibly client-reported,
    // therefore untrusted) move direction. Speed and dt are fixed server-owned
    // constants: a modified client sending an inflated magnitude gains nothing,
    // the direction is normalized to unit length first.
    inline Vec2 StepPlayer(Vec2 p, float mx, float my) {
        const float len = std::sqrt(mx * mx + my * my);
        if (len > 1.0f) {
            mx /= len;
            my /= len;
        }
        p.x = Clamp(p.x + mx * kMoveSpeed * kTickDt, kPlayerRadius, kArenaW - kPlayerRadius);
        p.y = Clamp(p.y + my * kMoveSpeed * kTickDt, kPlayerRadius, kArenaH - kPlayerRadius);
        return ResolveWall(p, kPlayerRadius);
    }

    inline Vec2 StepRocket(Vec2 p, Vec2 vel) {
        return { p.x + vel.x * kTickDt, p.y + vel.y * kTickDt };
    }

    inline bool InBounds(Vec2 p) {
        return p.x >= 0.0f && p.x <= kArenaW && p.y >= 0.0f && p.y <= kArenaH;
    }

    inline bool CircleCircle(Vec2 a, float ra, Vec2 b, float rb) {
        const float dx = a.x - b.x, dy = a.y - b.y;
        const float rr = ra + rb;
        return dx * dx + dy * dy <= rr * rr;
    }

    inline bool CircleHitsWall(Vec2 c, float r) {
        const float minx = kWallX, miny = kWallY, maxx = kWallX + kWallW, maxy = kWallY + kWallH;
        const float qx = Clamp(c.x, minx, maxx);
        const float qy = Clamp(c.y, miny, maxy);
        const float dx = c.x - qx, dy = c.y - qy;
        return dx * dx + dy * dy <= r * r;
    }

    // Smallest t >= 0 where ray (o + t*d, d unit) enters the circle, if any.
    inline bool RayCircle(Vec2 o, Vec2 d, Vec2 c, float r, float& tOut) {
        const Vec2 m = { o.x - c.x, o.y - c.y };
        const float b = m.x * d.x + m.y * d.y;
        const float cc = m.x * m.x + m.y * m.y - r * r;
        if (cc > 0.0f && b > 0.0f) return false;// origin outside, pointing away
        const float disc = b * b - cc;
        if (disc < 0.0f) return false;
        float t = -b - std::sqrt(disc);
        if (t < 0.0f) t = 0.0f;// origin already inside
        tOut = t;
        return true;
    }

    // Slab test: smallest t >= 0 where the ray enters the wall AABB, if any.
    inline bool RayWall(Vec2 o, Vec2 d, float& tOut) {
        const float minx = kWallX, miny = kWallY, maxx = kWallX + kWallW, maxy = kWallY + kWallH;
        float tmin = 0.0f, tmax = 1e30f;
        if (std::fabs(d.x) < 1e-8f) {
            if (o.x < minx || o.x > maxx) return false;
        } else {
            const float inv = 1.0f / d.x;
            float t1 = (minx - o.x) * inv, t2 = (maxx - o.x) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
        }
        if (std::fabs(d.y) < 1e-8f) {
            if (o.y < miny || o.y > maxy) return false;
        } else {
            const float inv = 1.0f / d.y;
            float t1 = (miny - o.y) * inv, t2 = (maxy - o.y) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
        }
        if (tmax < tmin) return false;
        tOut = tmin;
        return true;
    }

    // Hitscan (railgun): infinite-range ray from shooter along a unit aim.
    // Hits `target` only if the target is nearer than the wall along the ray
    // (so the wall genuinely occludes). `target` is whatever position the
    // caller decides to test — the server passes the lag-compensated
    // (rewound) target position here, which is what makes it favor-the-shooter.
    inline bool RailHits(Vec2 shooter, Vec2 aim, Vec2 target) {
        float tHit;
        if (!RayCircle(shooter, aim, target, kPlayerRadius, tHit)) return false;
        float tWall;
        if (RayWall(shooter, aim, tWall) && tWall < tHit) return false;// occluded
        return true;
    }

}// namespace sim

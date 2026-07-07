#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Movement / weapon / world geometry shared by the server (authoritative) and
// the client (prediction of its own movement, cosmetic prediction of its own
// rockets). Sharing this math is what lets client prediction agree with the
// server exactly — reconciliation here is textbook rewind-replay, so "agree
// exactly" has to be literally true or the prediction visibly fights the
// server.
//
// 3D arena shooter (Quake lineage). No physics engine on the authoritative
// path (Bullet/Box2D): server-authoritative single simulator, hand-rolled
// kinematics, so the server stays a lean dependency-free class and client and
// server run the *same* movement code. Movement is on the XZ ground plane
// (no jump/gravity in v1); Y is up. A player's authoritative position is its
// foot point on the floor (y = 0); the capsule and eye rise from there.
//
// What this dimension buys over a 2D version, netcode-wise: view angles
// (yaw/pitch) must now be replicated and quantized, movement is yaw-relative
// so the input carries the view yaw, and hit detection is ray-vs-capsule in
// 3D — the real "favor the shooter" surface.
namespace sim {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    constexpr float kTickRateHz = 60.0f;
    constexpr float kTickDt = 1.0f / kTickRateHz;

    constexpr float kArenaHalf = 12.0f;// arena spans [-12, 12] in X and Z
    constexpr float kFloorY = 0.0f;
    constexpr float kCeilingY = 6.0f;

    constexpr float kMoveSpeed = 6.0f;// m/s
    constexpr float kCapsuleRadius = 0.4f;
    constexpr float kCapsuleHeight = 1.8f;// foot (y=0) to head (y=1.8)
    constexpr float kEyeHeight = 1.6f;

    constexpr float kRocketSpeed = 22.0f;// m/s
    constexpr float kRocketRadius = 0.25f;

    constexpr int kMaxHealth = 100;
    constexpr int kRailDamage = 50;// 2 hits to down
    constexpr int kRocketDamage = 50;
    constexpr float kRespawnDelay = 1.5f;

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline Vec3 operator+(Vec3 a, Vec3 b) {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }
    inline Vec3 operator-(Vec3 a, Vec3 b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }
    inline Vec3 operator*(Vec3 a, float s) {
        return { a.x * s, a.y * s, a.z * s };
    }
    inline float Dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Axis-aligned box obstacle (full 3D extents). A tall central pillar plus
    // a couple of low crates: the pillar is the cover whose shadow makes the
    // "killed behind cover" downside of favor-the-shooter lag comp observable.
    struct Box {
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    };
    constexpr int kNumBoxes = 3;
    inline const Box* Boxes() {
        static const Box boxes[kNumBoxes] = {
            { -1.0f, 0.0f, -1.0f, 1.0f, 2.6f, 1.0f },// central pillar (occludes standing players)
            { -7.0f, 0.0f, 3.5f, -5.0f, 1.0f, 5.5f },// low crate (does not occlude a standing capsule)
            { 5.0f, 0.0f, -5.5f, 7.0f, 1.0f, -3.5f },// low crate
        };
        return boxes;
    }

    // Facing helpers. yaw=0 looks toward -Z; +yaw turns toward +X.
    inline Vec3 ForwardDir(float yaw, float pitch) {
        const float cp = std::cos(pitch);
        return { std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp };
    }
    inline void GroundBasis(float yaw, float& fx, float& fz, float& rx, float& rz) {
        fx = std::sin(yaw);
        fz = -std::cos(yaw);// forward (ground)
        rx = std::cos(yaw);
        rz = std::sin(yaw);// right (ground)
    }

    // Circle (player radius, in XZ) pushed out of a box's XZ rectangle.
    inline void ResolveBoxXZ(float& px, float& pz, float r, const Box& b) {
        const float cx = Clamp(px, b.minX, b.maxX);
        const float cz = Clamp(pz, b.minZ, b.maxZ);
        const float dx = px - cx, dz = pz - cz;
        const float d2 = dx * dx + dz * dz;
        if (d2 > r * r) return;
        if (d2 > 1e-6f) {
            const float d = std::sqrt(d2);
            const float push = r - d;
            px += dx / d * push;
            pz += dz / d * push;
        } else {
            const float left = px - b.minX, right = b.maxX - px, front = pz - b.minZ, back = b.maxZ - pz;
            const float m = std::min(std::min(left, right), std::min(front, back));
            if (m == left)
                px = b.minX - r;
            else if (m == right)
                px = b.maxX + r;
            else if (m == front)
                pz = b.minZ - r;
            else
                pz = b.maxZ + r;
        }
    }

    // Advances a foot position by one tick given a (client-reported, untrusted)
    // move intent expressed as forward/strafe in the player's own view frame.
    // Speed and dt are fixed server constants; yaw is taken from the input so
    // the server reproduces exactly the movement the client predicted.
    inline Vec3 StepPlayer(Vec3 foot, float forward, float strafe, float yaw) {
        float fx, fz, rx, rz;
        GroundBasis(yaw, fx, fz, rx, rz);
        float mx = fx * forward + rx * strafe;
        float mz = fz * forward + rz * strafe;
        const float len = std::sqrt(mx * mx + mz * mz);
        if (len > 1.0f) {
            mx /= len;
            mz /= len;
        }
        foot.x = Clamp(foot.x + mx * kMoveSpeed * kTickDt, -kArenaHalf + kCapsuleRadius, kArenaHalf - kCapsuleRadius);
        foot.z = Clamp(foot.z + mz * kMoveSpeed * kTickDt, -kArenaHalf + kCapsuleRadius, kArenaHalf - kCapsuleRadius);
        const Box* boxes = Boxes();
        for (int i = 0; i < kNumBoxes; i++)
            ResolveBoxXZ(foot.x, foot.z, kCapsuleRadius, boxes[i]);
        foot.y = kFloorY;
        return foot;
    }

    inline Vec3 StepRocket(Vec3 p, Vec3 vel) {
        return p + vel * kTickDt;
    }

    inline bool InBounds(Vec3 p) {
        return p.x >= -kArenaHalf && p.x <= kArenaHalf && p.z >= -kArenaHalf && p.z <= kArenaHalf && p.y >= kFloorY
               && p.y <= kCeilingY;
    }

    // Capsule spanning a foot position: a vertical segment [A,B] of radius
    // kCapsuleRadius. Yaw-invariant (radially symmetric), which is why the
    // server needs only *position* history to rewind a target for lag comp,
    // not orientation history.
    inline void CapsuleSegment(Vec3 foot, Vec3& a, Vec3& b) {
        a = { foot.x, foot.y + kCapsuleRadius, foot.z };
        b = { foot.x, foot.y + kCapsuleHeight - kCapsuleRadius, foot.z };
    }

    inline float ClampF(float v, float lo, float hi) {
        return Clamp(v, lo, hi);
    }

    // Squared distance between segment [p1,q1] and segment [p2,q2] (Ericson,
    // Real-Time Collision Detection). Also returns the closest-point params s,t.
    inline float ClosestSegSeg(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, float& s, float& t) {
        Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
        float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
        if (a <= 1e-8f && e <= 1e-8f) {
            s = t = 0.0f;
            Vec3 d = p1 - p2;
            return Dot(d, d);
        }
        if (a <= 1e-8f) {
            s = 0.0f;
            t = ClampF(f / e, 0.0f, 1.0f);
        } else {
            float c = Dot(d1, r);
            if (e <= 1e-8f) {
                t = 0.0f;
                s = ClampF(-c / a, 0.0f, 1.0f);
            } else {
                float bb = Dot(d1, d2);
                float denom = a * e - bb * bb;
                s = (denom != 0.0f) ? ClampF((bb * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                t = (bb * s + f) / e;
                if (t < 0.0f) {
                    t = 0.0f;
                    s = ClampF(-c / a, 0.0f, 1.0f);
                } else if (t > 1.0f) {
                    t = 1.0f;
                    s = ClampF((bb - c) / a, 0.0f, 1.0f);
                }
            }
        }
        Vec3 c1 = p1 + d1 * s, c2 = p2 + d2 * t;
        Vec3 d = c1 - c2;
        return Dot(d, d);
    }

    // Ray (origin o, unit dir d) vs vertical capsule [A,B] radius kCapsuleRadius.
    // Returns whether it hits and, if so, the approximate distance along the
    // ray (tRay) at the closest approach — used to order against occluders.
    inline bool RayCapsule(Vec3 o, Vec3 d, Vec3 a, Vec3 b, float& tRay) {
        constexpr float kFar = 1000.0f;
        Vec3 rayEnd = o + d * kFar;
        float s, t;
        float dist2 = ClosestSegSeg(o, rayEnd, a, b, s, t);
        if (dist2 > kCapsuleRadius * kCapsuleRadius) return false;
        tRay = s * kFar;
        return tRay >= 0.0f;
    }

    // Ray vs box AABB (3-slab). Smallest t >= 0 of entry, if any.
    inline bool RayBox(Vec3 o, Vec3 d, const Box& box, float& tOut) {
        float tmin = 0.0f, tmax = 1e30f;
        const float bmin[3] = { box.minX, box.minY, box.minZ };
        const float bmax[3] = { box.maxX, box.maxY, box.maxZ };
        const float oo[3] = { o.x, o.y, o.z };
        const float dd[3] = { d.x, d.y, d.z };
        for (int i = 0; i < 3; i++) {
            if (std::fabs(dd[i]) < 1e-8f) {
                if (oo[i] < bmin[i] || oo[i] > bmax[i]) return false;
            } else {
                const float inv = 1.0f / dd[i];
                float t1 = (bmin[i] - oo[i]) * inv, t2 = (bmax[i] - oo[i]) * inv;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
            }
        }
        if (tmax < tmin) return false;
        tOut = tmin;
        return true;
    }

    // Hitscan (railgun): ray from the shooter's eye along its view direction.
    // Hits the target capsule only if no box occludes it first. `targetFoot`
    // is whatever the caller decides to test — the server passes the
    // lag-compensated (rewound) foot position, which is what makes it
    // favor-the-shooter.
    inline bool RailHits(Vec3 eye, Vec3 viewDir, Vec3 targetFoot) {
        Vec3 a, b;
        CapsuleSegment(targetFoot, a, b);
        float tHit;
        if (!RayCapsule(eye, viewDir, a, b, tHit)) return false;
        const Box* boxes = Boxes();
        for (int i = 0; i < kNumBoxes; i++) {
            float tBox;
            if (RayBox(eye, viewDir, boxes[i], tBox) && tBox < tHit) return false;// occluded
        }
        return true;
    }

    // Distance from a point (rocket center) to a vertical capsule; used for
    // rocket-vs-player collision.
    inline bool PointHitsCapsule(Vec3 p, Vec3 targetFoot, float extraRadius) {
        Vec3 a, b;
        CapsuleSegment(targetFoot, a, b);
        Vec3 ab = b - a, ap = p - a;
        float tp = Dot(ap, ab);
        float denom = Dot(ab, ab);
        float u = denom > 1e-8f ? ClampF(tp / denom, 0.0f, 1.0f) : 0.0f;
        Vec3 c = a + ab * u;
        Vec3 d = p - c;
        const float rr = kCapsuleRadius + extraRadius;
        return Dot(d, d) <= rr * rr;
    }

    inline bool PointHitsBox(Vec3 p, float r) {
        const Box* boxes = Boxes();
        for (int i = 0; i < kNumBoxes; i++) {
            const Box& b = boxes[i];
            const float cx = Clamp(p.x, b.minX, b.maxX);
            const float cy = Clamp(p.y, b.minY, b.maxY);
            const float cz = Clamp(p.z, b.minZ, b.maxZ);
            const float dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
            if (dx * dx + dy * dy + dz * dz <= r * r) return true;
        }
        return false;
    }

    // Shortest-arc interpolation of a yaw angle (handles the 2π wraparound
    // that a naive lerp would get wrong when crossing 0/2π).
    inline float LerpYaw(float a, float b, float t) {
        float diff = std::fmod(b - a + kPi + kTwoPi, kTwoPi) - kPi;
        return a + diff * t;
    }

}// namespace sim

#pragma once
#include "easing.hpp"
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Animation clips (shared, data-only assets)
//
// These are the passive data the AnimatorComponent players sample. They carry
// no playback state (no playhead, no speed) — one clip instance can back any
// number of simultaneous players. Playback state lives in the component; the
// clip is a pure function of time. All three live in AnimationLibrary and are
// referred to by lightweight handles so they can be shared and hot-swapped.
// ─────────────────────────────────────────────────────────────────────────────

// ── Handles ──────────────────────────────────────────────────────────────────
// id == 0 means "invalid / unset". Distinct types so a flipbook handle can't be
// passed where a timeline handle is expected.
struct FlipbookClipHandle {
    uint32_t id = 0;
    bool IsValid() const {
        return id != 0;
    }
    bool operator==(const FlipbookClipHandle& o) const {
        return id == o.id;
    }
};
struct TimelineHandle {
    uint32_t id = 0;
    bool IsValid() const {
        return id != 0;
    }
    bool operator==(const TimelineHandle& o) const {
        return id == o.id;
    }
};
struct VATClipHandle {
    uint32_t id = 0;
    bool IsValid() const {
        return id != 0;
    }
    bool operator==(const VATClipHandle& o) const {
        return id == o.id;
    }
};

// ── Flipbook (frame-based sprite animation) ──────────────────────────────────
struct FlipbookFrame {
    glm::vec2 uvMin{ 0.0f, 0.0f };
    glm::vec2 uvMax{ 1.0f, 1.0f };
    float duration = 0.1f;// seconds this frame is shown
    int eventId = -1;// >= 0 → fire a frame event when playback enters this frame
};

struct FlipbookClip {
    std::string name;
    std::vector<FlipbookFrame> frames;

    // Sum of frame durations. Recomputed on demand (frame lists are small and
    // built once); the component caches prefix sums for O(log n) sampling.
    float GetDuration() const;

    // Build a strip from a tileset laid out left-to-right, top-to-bottom.
    // tilesetSize is the tile grid dimensions (cols, rows); tileIndices are
    // linear indices into that grid. Every frame gets frameDuration seconds.
    static FlipbookClip
        FromTileset(std::string name, glm::vec2 tilesetSize, const std::vector<int>& tileIndices, float frameDuration);

    // Build from explicit (col, row) cells of a cols×rows grid. This is the
    // addressing the RPG example's ad-hoc SpriteAnimator used.
    static FlipbookClip FromGrid(
        std::string name, int cols, int rows, const std::vector<std::pair<int, int>>& cells, float frameDuration
    );
};

// ── Action timeline (property tweens as keyframes) ───────────────────────────
enum class ActionProperty {
    Position,// vec3 world/local position
    Rotation,// vec3 euler radians
    Scale,// vec3
    Color,// vec4 tint (sprite/text/material)
    Alpha,// float in .x → color.a only
    Custom,// float in .x, routed to a callback by customId
    RotationQuat,// quaternion (x,y,z,w) — slerp'd; glTF/USD node rotation
};

struct ActionKey {
    float time = 0.0f;// seconds from timeline start
    glm::vec4 value{ 0.0f };// interpreted per ActionProperty (scalars in .x)
    EasingType easing = EasingType::Linear;// eases the segment ending at this key
};

struct ActionTrack {
    ActionProperty property = ActionProperty::Position;
    int customId = 0;// routes ActionProperty::Custom tracks
    std::vector<ActionKey> keys;// kept sorted by time

    // Sample the track at t (seconds). Binary-searches the surrounding keys and
    // returns the eased lerp; clamps to the first/last key outside the range.
    // Returns a zero vector for an empty track.
    glm::vec4 Sample(float t) const;
};

struct TimelineEvent {
    float time = 0.0f;
    int eventId = 0;
};

struct ActionTimeline {
    std::string name;
    float duration = 0.0f;// max key/event time; cached by Recompute()
    std::vector<ActionTrack> tracks;
    std::vector<TimelineEvent> events;// sorted by time

    // Recompute `duration` from the tracks/events (call after building).
    void Recompute();
};

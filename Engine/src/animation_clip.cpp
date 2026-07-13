#include "animation_clip.hpp"
#include <algorithm>

// ── FlipbookClip ─────────────────────────────────────────────────────────────

float FlipbookClip::GetDuration() const {
    float total = 0.0f;
    for (const auto& f : frames) total += f.duration;
    return total;
}

FlipbookClip FlipbookClip::FromTileset(
    std::string name, glm::vec2 tilesetSize, const std::vector<int>& tileIndices, float frameDuration
) {
    FlipbookClip clip;
    clip.name = std::move(name);
    const int cols = std::max(1, static_cast<int>(tilesetSize.x));
    const int rows = std::max(1, static_cast<int>(tilesetSize.y));
    const float du = 1.0f / static_cast<float>(cols);
    const float dv = 1.0f / static_cast<float>(rows);
    clip.frames.reserve(tileIndices.size());
    for (int idx : tileIndices) {
        const int col = idx % cols;
        const int row = idx / cols;
        FlipbookFrame f;
        f.uvMin = { col * du, row * dv };
        f.uvMax = { (col + 1) * du, (row + 1) * dv };
        f.duration = frameDuration;
        clip.frames.push_back(f);
    }
    return clip;
}

FlipbookClip FlipbookClip::FromGrid(
    std::string name, int cols, int rows, const std::vector<std::pair<int, int>>& cells, float frameDuration
) {
    FlipbookClip clip;
    clip.name = std::move(name);
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    const float du = 1.0f / static_cast<float>(cols);
    const float dv = 1.0f / static_cast<float>(rows);
    clip.frames.reserve(cells.size());
    for (const auto& [col, row] : cells) {
        FlipbookFrame f;
        f.uvMin = { col * du, row * dv };
        f.uvMax = { (col + 1) * du, (row + 1) * dv };
        f.duration = frameDuration;
        clip.frames.push_back(f);
    }
    return clip;
}

// ── ActionTrack ──────────────────────────────────────────────────────────────

glm::vec4 ActionTrack::Sample(float t) const {
    if (keys.empty()) return glm::vec4(0.0f);
    if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    // First key strictly after t (keys are sorted by time).
    auto hi = std::upper_bound(keys.begin(), keys.end(), t, [](float time, const ActionKey& k) {
        return time < k.time;
    });
    const ActionKey& b = *hi;
    const ActionKey& a = *(hi - 1);

    const float span = b.time - a.time;
    float u = span > 0.0f ? (t - a.time) / span : 1.0f;
    u = ApplyEasing(u, b.easing);// segment easing is authored on the destination key
    return a.value + (b.value - a.value) * u;
}

// ── ActionTimeline ───────────────────────────────────────────────────────────

void ActionTimeline::Recompute() {
    float d = 0.0f;
    for (const auto& tr : tracks) {
        if (!tr.keys.empty()) d = std::max(d, tr.keys.back().time);
    }
    for (const auto& e : events) d = std::max(d, e.time);
    duration = d;
}

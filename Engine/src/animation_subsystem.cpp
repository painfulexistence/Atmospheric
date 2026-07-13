#include "animation_subsystem.hpp"
#include "animator_component.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "vat.hpp"
#include <algorithm>

AnimationSubsystem* AnimationSubsystem::_instance = nullptr;

// ── AnimationLibrary ─────────────────────────────────────────────────────────

FlipbookClipHandle AnimationLibrary::AddFlipbook(FlipbookClip clip) {
    const std::string name = clip.name;
    _flipbooks.push_back(std::move(clip));
    const uint32_t id = static_cast<uint32_t>(_flipbooks.size());// index + 1
    if (!name.empty()) _flipbookByName[name] = id;
    return { id };
}

TimelineHandle AnimationLibrary::AddTimeline(ActionTimeline timeline) {
    timeline.Recompute();
    const std::string name = timeline.name;
    _timelines.push_back(std::move(timeline));
    const uint32_t id = static_cast<uint32_t>(_timelines.size());
    if (!name.empty()) _timelineByName[name] = id;
    return { id };
}

VATClipHandle AnimationLibrary::AddVATClip(std::string name, std::unique_ptr<VATClip> clip) {
    _vatClips.push_back(std::move(clip));
    _vatNames.push_back(name);
    const uint32_t id = static_cast<uint32_t>(_vatClips.size());
    if (!name.empty()) _vatByName[name] = id;
    return { id };
}

const FlipbookClip* AnimationLibrary::GetFlipbook(FlipbookClipHandle h) const {
    if (!h.IsValid() || h.id > _flipbooks.size()) return nullptr;
    return &_flipbooks[h.id - 1];
}

const ActionTimeline* AnimationLibrary::GetTimeline(TimelineHandle h) const {
    if (!h.IsValid() || h.id > _timelines.size()) return nullptr;
    return &_timelines[h.id - 1];
}

VATClip* AnimationLibrary::GetVATClip(VATClipHandle h) const {
    if (!h.IsValid() || h.id > _vatClips.size()) return nullptr;
    return _vatClips[h.id - 1].get();
}

FlipbookClipHandle AnimationLibrary::FindFlipbook(const std::string& name) const {
    auto it = _flipbookByName.find(name);
    return it != _flipbookByName.end() ? FlipbookClipHandle{ it->second } : FlipbookClipHandle{};
}

TimelineHandle AnimationLibrary::FindTimeline(const std::string& name) const {
    auto it = _timelineByName.find(name);
    return it != _timelineByName.end() ? TimelineHandle{ it->second } : TimelineHandle{};
}

VATClipHandle AnimationLibrary::FindVATClip(const std::string& name) const {
    auto it = _vatByName.find(name);
    return it != _vatByName.end() ? VATClipHandle{ it->second } : VATClipHandle{};
}

void AnimationLibrary::Clear() {
    _flipbooks.clear();
    _timelines.clear();
    _vatClips.clear();
    _vatNames.clear();
    _flipbookByName.clear();
    _timelineByName.clear();
    _vatByName.clear();
}

// ── AnimationSubsystem ───────────────────────────────────────────────────────

AnimationSubsystem::AnimationSubsystem() {
    _instance = this;
}

AnimationSubsystem::~AnimationSubsystem() {
    if (_instance == this) _instance = nullptr;
}

void AnimationSubsystem::Init(Application* app) {
    _app = app;
    _initialized = true;
}

float AnimationSubsystem::GetGroupTimeScale(const std::string& group) const {
    auto it = _groupScales.find(group);
    return it != _groupScales.end() ? it->second : 1.0f;
}

void AnimationSubsystem::Register(AnimatorComponent* player) {
    if (!player) return;
    if (_iterating) {
        _pendingAdd.push_back(player);
    } else {
        _players.push_back(player);
    }
}

void AnimationSubsystem::Unregister(AnimatorComponent* player) {
    if (!player) return;
    if (_iterating) {
        _pendingRemove.push_back(player);
    } else {
        _players.erase(std::remove(_players.begin(), _players.end(), player), _players.end());
    }
}

void AnimationSubsystem::Process(float dt) {
    _iterating = true;
    for (AnimatorComponent* player : _players) {
        if (!player || !player->enabled || !player->_state.playing) continue;
        if (player->gameObject && !player->gameObject->isActive) continue;

        const float scaled = dt * _timeScale * GetGroupTimeScale(player->_state.group);
        player->Advance(scaled);
    }
    _iterating = false;

    // Apply registrations deferred during the walk.
    for (AnimatorComponent* p : _pendingRemove) {
        _players.erase(std::remove(_players.begin(), _players.end(), p), _players.end());
    }
    _pendingRemove.clear();
    for (AnimatorComponent* p : _pendingAdd) _players.push_back(p);
    _pendingAdd.clear();
}

void AnimationSubsystem::DrawImGui(float dt) {
    if (!ImGui::Begin("Animation")) {
        ImGui::End();
        return;
    }

    ImGui::SliderFloat("Global time scale", &_timeScale, 0.0f, 2.0f);
    ImGui::Text("Active players: %zu", _players.size());
    ImGui::Separator();

    if (ImGui::BeginTable("players", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Owner");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Playing");
        ImGui::TableHeadersRow();
        for (AnimatorComponent* p : _players) {
            if (!p) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p->gameObject ? p->gameObject->GetName().c_str() : "?");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p->GetName().c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f / %.2f", p->GetTime(), p->GetDuration());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p->IsPlaying() ? "yes" : "no");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

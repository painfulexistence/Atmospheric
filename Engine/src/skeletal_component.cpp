#include "skeletal_component.hpp"
#include "animation_subsystem.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "gfx_factory.hpp"
#include "imgui.h"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_renderer_component.hpp"
#include "skeleton.hpp"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

static AnimationLibrary* SkelLib() {
    auto* sub = AnimationSubsystem::Get();
    return sub ? &sub->Library() : nullptr;
}

SkeletalComponent::SkeletalComponent(
    GameObject* owner, MeshHandle mesh, SkeletonHandle skeleton, const SkeletalProps& props
)
  : AnimatorComponent(owner), _mesh(mesh), _skeleton(skeleton) {
    _state.wrap = props.wrap;
    _state.speed = props.speed;
    _state.playing = props.playing;
    _state.time = props.startTime;

    EnsureMaterial();
    owner->AddComponent<MeshRendererComponent>(_mesh);
}

SkeletalComponent::~SkeletalComponent() {
    if (_boneTex) GfxFactory::ReleaseTexture(_boneTex);
}

void SkeletalComponent::EnsureMaterial() {
    auto& am = AssetManager::Get();
    if (!_material) {
        _material = am.CreateSkinnedMaterial();
        if (Mesh* meshPtr = am.GetMeshPtr(_mesh)) meshPtr->SetMaterial(am.GetMaterialHandle(_material));
    }
}

void SkeletalComponent::OnAttach() {
    AnimatorComponent::OnAttach();// register with the subsystem
    Evaluate(_state.time);// show the bind/first pose immediately
}

void SkeletalComponent::AddClip(SkeletonClipHandle clip) {
    if (clip.IsValid() && std::find(_clips.begin(), _clips.end(), clip) == _clips.end()) _clips.push_back(clip);
}

bool SkeletalComponent::Play(const std::string& clipName) {
    auto* lib = SkelLib();
    if (!lib) return false;
    // Resolve within THIS component's own clips (names collide across models).
    for (SkeletonClipHandle h : _clips) {
        const SkeletonClip* c = lib->GetSkeletonClip(h);
        if (c && c->name == clipName) {
            _active = h;
            _activeName = c->name;
            _state.time = 0.0f;
            _state.playing = true;
            Evaluate(0.0f);
            return true;
        }
    }
    return false;
}

void SkeletalComponent::SetClip(SkeletonClipHandle clip) {
    _active = clip;
    _state.time = 0.0f;
    if (auto* lib = SkelLib()) {
        const SkeletonClip* c = lib->GetSkeletonClip(clip);
        _activeName = c ? c->name : "";
    }
    Evaluate(_state.time);
}

float SkeletalComponent::GetDuration() const {
    auto* lib = SkelLib();
    const SkeletonClip* c = lib ? lib->GetSkeletonClip(_active) : nullptr;
    return c ? c->duration : 0.0f;
}

void SkeletalComponent::Evaluate(float time) {
    if (!_material) return;
    const Skeleton* skel = AssetManager::Get().GetSkeleton(_skeleton);
    if (!skel || skel->joints.empty()) return;

    auto* lib = SkelLib();
    const SkeletonClip* clip = lib ? lib->GetSkeletonClip(_active) : nullptr;// null → bind pose

    const size_t n = skel->joints.size();
    _model.resize(n);
    _palette.resize(n);

    for (size_t j = 0; j < n; ++j) {
        const Joint& jt = skel->joints[j];
        glm::vec3 T = jt.bindTranslation;
        glm::quat R = jt.bindRotation;
        glm::vec3 S = jt.bindScale;
        if (clip) {
            for (const auto& ch : clip->channels) {
                if (ch.joint == static_cast<int>(j)) {
                    T = SampleVec3Track(ch.translation, time, T);
                    R = SampleQuatTrack(ch.rotation, time, R);
                    S = SampleVec3Track(ch.scale, time, S);
                    break;
                }
            }
        }
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), T) * glm::mat4_cast(R) * glm::scale(glm::mat4(1.0f), S);
        // Skeleton is topologically ordered, so the parent is already computed.
        _model[j] = (jt.parent >= 0 && jt.parent < static_cast<int>(j)) ? _model[jt.parent] * local : local;
        _palette[j] = _model[j] * jt.inverseBind;
    }

    UploadPalette();
}

void SkeletalComponent::UploadPalette() {
    const int jointCount = static_cast<int>(_palette.size());
    if (jointCount == 0) return;
    // RGBA32F, width = jointCount*4 (the four mat4 columns per joint), height 1.
    // glm::mat4 is column-major and contiguous, so the palette uploads directly.
    // NOTE: recreated each frame — a small texture, but a persistent texture with
    // a sub-image update would avoid the churn (needs a GfxFactory update API).
    if (_boneTex) GfxFactory::ReleaseTexture(_boneTex);
    _boneTex = GfxFactory::UploadTextureRGBA32F(reinterpret_cast<const float*>(_palette.data()), jointCount * 4, 1);
    _material->boneTexture = _boneTex;
    _material->jointCount = jointCount;
}

void SkeletalComponent::DrawImGui() {
    // Editor wraps each component in its own CollapsingHeader(GetName()) + PushID.
    const Skeleton* skel = AssetManager::Get().GetSkeleton(_skeleton);
    ImGui::Text(
        "Joints: %zu   Clip: %s", skel ? skel->JointCount() : 0, _activeName.empty() ? "(bind)" : _activeName.c_str()
    );
    AnimatorComponent::DrawImGui();
}

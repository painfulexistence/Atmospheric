#include "vat_component.hpp"
#include "animation_subsystem.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_renderer_component.hpp"
#include "vat.hpp"
#include <utility>

static AnimationLibrary* VATLib() {
    auto* sub = AnimationSubsystem::Get();
    return sub ? &sub->Library() : nullptr;
}

VATComponent::VATComponent(GameObject* owner, MeshHandle mesh, std::unique_ptr<VATClip> clip, const VATProps& props)
  : AnimatorComponent(owner), _mesh(mesh) {
    if (auto* lib = VATLib()) _clip = lib->AddVATClip("", std::move(clip));

    _state.wrap = props.wrap;
    _state.speed = props.speed;
    _state.playing = props.playing;
    _state.time = props.startTime;

    EnsureMaterial();
    owner->AddComponent<MeshRendererComponent>(_mesh);
}

VATComponent::VATComponent(GameObject* owner, MeshHandle mesh, VATClipHandle clip, const VATProps& props)
  : AnimatorComponent(owner), _mesh(mesh), _clip(clip) {
    _state.wrap = props.wrap;
    _state.speed = props.speed;
    _state.playing = props.playing;
    _state.time = props.startTime;

    EnsureMaterial();
    owner->AddComponent<MeshRendererComponent>(_mesh);
}

void VATComponent::EnsureMaterial() {
    auto& am = AssetManager::Get();
    if (!_material) {
        _material = am.CreateVATMaterial();
        if (Mesh* meshPtr = am.GetMeshPtr(_mesh)) meshPtr->SetMaterial(am.GetMaterialHandle(_material));
    }
    _material->clip = GetClip();
    _material->normalizedTime = GetNormalizedTime();
}

void VATComponent::OnAttach() {
    AnimatorComponent::OnAttach();// register with the subsystem
    Evaluate(_state.time);// show the start frame immediately
}

VATClip* VATComponent::GetClip() const {
    auto* lib = VATLib();
    return lib ? lib->GetVATClip(_clip) : nullptr;
}

float VATComponent::GetDuration() const {
    VATClip* clip = GetClip();
    return clip ? clip->GetDuration() : 0.0f;
}

void VATComponent::SetClip(VATClipHandle clip) {
    _clip = clip;
    _state.time = 0.0f;
    if (_material) _material->clip = GetClip();
    Evaluate(_state.time);
}

void VATComponent::Evaluate(float time) {
    if (!_material) return;
    const float dur = GetDuration();
    _material->normalizedTime = dur > 0.0f ? time / dur : 0.0f;
}

void VATComponent::DrawImGui() {
    // Editor wraps each component in its own CollapsingHeader(GetName()) + PushID.
    VATClip* clip = GetClip();
    if (clip) {
        ImGui::Text(
            "Clip: %u verts x %u frames @ %.1f fps", clip->GetVertCount(), clip->GetFrameCount(), clip->GetFrameRate()
        );
    }
    AnimatorComponent::DrawImGui();
}

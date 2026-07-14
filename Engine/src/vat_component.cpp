#include "vat_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_renderer_component.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

VATComponent::VATComponent(GameObject* owner, MeshHandle mesh, std::unique_ptr<VATClip> clip, const VATProps& props)
  : _mesh(mesh), _clip(std::move(clip)), _props(props), _time(props.startTime) {
    gameObject = owner;

    auto& am = AssetManager::Get();
    _material = am.CreateVATMaterial();
    _material->clip = _clip.get();
    _material->normalizedTime = _time;

    if (Mesh* meshPtr = am.GetMeshPtr(_mesh)) meshPtr->SetMaterial(am.GetMaterialHandle(_material));

    owner->AddComponent<MeshRendererComponent>(_mesh);
}

void VATComponent::OnAttach() {
    if (_material) _material->normalizedTime = _time;
}

void VATComponent::OnTick(float dt) {
    if (!_material || !_clip) return;

    const float duration = _clip->GetDuration();
    if (_props.playing && duration > 0.0f) {
        _time += (dt * _props.speed) / duration;
        if (_props.loop) {
            // fmod keeps the playhead in [0, 1); negative speeds wrap too.
            _time -= std::floor(_time);
        } else {
            _time = std::clamp(_time, 0.0f, 1.0f);
        }
    }
    _material->normalizedTime = _time;
}

void VATComponent::DrawImGui() {
    if (!_clip) return;
    ImGui::Text(
        "Clip: %u verts x %u frames @ %.1f fps", _clip->GetVertCount(), _clip->GetFrameCount(), _clip->GetFrameRate()
    );
    ImGui::Checkbox("Playing", &_props.playing);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &_props.loop);
    ImGui::DragFloat("Speed", &_props.speed, 0.01f, -4.0f, 4.0f);
    if (ImGui::SliderFloat("Playhead", &_time, 0.0f, 1.0f) && _material) {
        _material->normalizedTime = _time;
    }
}

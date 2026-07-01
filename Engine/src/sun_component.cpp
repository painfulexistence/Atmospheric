#include "sun_component.hpp"
#include "application.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "imgui.h"
#include <algorithm>

SunComponent::SunComponent(glm::vec3 billboardColor,
                           float     billboardRadius,
                           float     height)
    : billboardColor(billboardColor)
    , billboardRadius(billboardRadius)
    , height(height)
{}

void SunComponent::OnAttach() {
    if (auto* gfx = gameObject->GetApp()->GetGraphicsServer()) {
        gfx->RegisterSun(this);
    }
}

void SunComponent::DrawImGui() {
    ImGui::DragFloat3("Billboard Color",  &billboardColor.x,  1.0f, 0.0f, 500.0f);
    ImGui::DragFloat("Billboard Radius", &billboardRadius,   0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Height",           &height,            1.0f);
}

void SunComponent::OnDetach() {
    if (auto* gfx = gameObject->GetApp()->GetGraphicsServer()) {
        auto& list = gfx->sunComponents;
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
}

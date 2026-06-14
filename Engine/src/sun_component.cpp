#include "sun_component.hpp"
#include "application.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
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

void SunComponent::OnDetach() {
    if (auto* gfx = gameObject->GetApp()->GetGraphicsServer()) {
        auto& list = gfx->sunComponents;
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
}

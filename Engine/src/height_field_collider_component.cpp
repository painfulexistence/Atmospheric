#include "height_field_collider_component.hpp"
#include "bullet_collision.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "rigidbody_component.hpp"

HeightFieldColliderComponent::HeightFieldColliderComponent(
    GameObject*                         owner,
    const std::shared_ptr<HeightField>& heightField,
    const HeightFieldColliderProps&     props
) {
    gameObject = owner;

    const int w = heightField->Width();
    const int d = heightField->Depth();
    _scaledGrid.resize(w * d);
    const auto& src = heightField->Grid();
    for (int i = 0; i < w * d; ++i)
        _scaledGrid[i] = src[i] * props.heightScale;

    _shape = new btHeightfieldTerrainShape(
        w, d,
        _scaledGrid.data(),
        1.0f,                       // heightScale parameter (data is pre-scaled)
        props.minHeight,
        props.maxHeight,
        1,                          // upAxis Y
        PHY_FLOAT,
        true                        // flipQuadEdges
    );

    owner->AddComponent<RigidbodyComponent>(RigidbodyProps{
        .mass        = props.mass,
        .friction    = props.friction,
        .restitution = props.restitution,
        .shape       = _shape,
        .useGravity  = false,
    });
}

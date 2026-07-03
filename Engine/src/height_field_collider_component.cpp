#include "height_field_collider_component.hpp"
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include "console.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "rigidbody_component.hpp"

HeightFieldColliderComponent::HeightFieldColliderComponent(
    GameObject*                         owner,
    const std::shared_ptr<HeightField>& heightField,
    const HeightFieldColliderProps&     props
) : _heightField(heightField), _heightScale(props.heightScale) {
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

    // Bullet heightfield has 1 unit per sample by default; scale XZ to match worldSize.
    const float xzScale = props.worldSize / float(w);
    _shape->setLocalScaling(btVector3(xzScale, 1.0f, xzScale));

    // btHeightfieldTerrainShape centres itself at (minHeight+maxHeight)/2 in Y.
    // Offset the rigidbody origin to compensate so the collider aligns with the
    // rendered mesh, which is displaced by heightScale*[0,1] above the GO position.
    const float bulletCenterY = (props.minHeight + props.maxHeight) * 0.5f;
    const float meshCenterY   = props.heightScale * 0.5f;
    const float yOffset       = meshCenterY - bulletCenterY;

    glm::vec3 pos = owner->GetPosition();
    pos.y += yOffset;

    auto* rb = static_cast<RigidbodyComponent*>(owner->AddComponent<RigidbodyComponent>(RigidbodyProps{
        .mass        = props.mass,
        .friction    = props.friction,
        .restitution = props.restitution,
        .shape       = _shape,
        .useGravity  = false,
    }));
    rb->SetWorldTransform(pos, owner->GetRotation());
}

void HeightFieldColliderComponent::SyncFromHeightField() {
    const int n = _heightField->Width() * _heightField->Depth();
    if (n != (int)_scaledGrid.size()) {
        // btHeightfieldTerrainShape stores its dimensions at construction;
        // a resolution change would require rebuilding the shape + rigidbody.
        ENGINE_LOG("HeightFieldCollider: resolution changed ({} -> {}), collider not updated",
                   _scaledGrid.size(), n);
        return;
    }
    const auto& src = _heightField->Grid();
    for (int i = 0; i < n; ++i)
        _scaledGrid[i] = src[i] * _heightScale;
}

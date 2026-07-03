#include "height_field_collider_component.hpp"
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include "game_object.hpp"
#include "height_field.hpp"
#include "rigidbody_component.hpp"

HeightFieldColliderComponent::HeightFieldColliderComponent(
    GameObject*                         owner,
    const std::shared_ptr<HeightField>& heightField,
    const HeightFieldColliderProps&     props
) : _heightField(heightField), _heightScale(props.heightScale) {
    gameObject = owner;

    _width = props.resolution > 0 ? props.resolution : heightField->Width();
    _depth = props.resolution > 0 ? props.resolution : heightField->Depth();
    _scaledGrid.resize((size_t)_width * _depth);
    SyncFromHeightField();

    _shape = std::make_unique<btHeightfieldTerrainShape>(
        _width, _depth,
        _scaledGrid.data(),
        1.0f,                       // heightScale parameter (data is pre-scaled)
        props.minHeight,
        props.maxHeight,
        1,                          // upAxis Y
        PHY_FLOAT,
        true                        // flipQuadEdges
    );

    // Bullet heightfield has 1 unit per sample by default; scale XZ to match worldSize.
    const float xzScale = props.worldSize / float(_width);
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
        .shape       = _shape.get(),
        .useGravity  = false,
    }));
    rb->SetWorldTransform(pos, owner->GetRotation());
}

// The rigidbody sibling only observes _shape (btRigidBody does not own its
// collision shape), and it is removed from the physics world in
// RigidbodyComponent::OnDetach before any component is destroyed, so freeing
// the shape here is safe.
HeightFieldColliderComponent::~HeightFieldColliderComponent() = default;

void HeightFieldColliderComponent::SyncFromHeightField() {
    // Bilinear resample so the collider resolution is independent of the
    // HeightField's — full copy when they match, decimation when they don't.
    if (_width == _heightField->Width() && _depth == _heightField->Depth()) {
        const auto& src = _heightField->Grid();
        for (size_t i = 0; i < _scaledGrid.size(); ++i)
            _scaledGrid[i] = src[i] * _heightScale;
        return;
    }
    for (int z = 0; z < _depth; ++z) {
        const float v = _depth > 1 ? z / float(_depth - 1) : 0.0f;
        for (int x = 0; x < _width; ++x) {
            const float u = _width > 1 ? x / float(_width - 1) : 0.0f;
            _scaledGrid[(size_t)z * _width + x] = _heightField->SampleNormalized(u, v) * _heightScale;
        }
    }
}

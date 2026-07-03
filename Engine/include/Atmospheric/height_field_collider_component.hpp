#pragma once
#include "component.hpp"
#include <memory>
#include <vector>

class HeightField;
class btCollisionShape;

struct HeightFieldColliderProps {
    float worldSize   = 1024.0f;
    float heightScale =   32.0f;
    float minHeight   =  -64.0f;
    float maxHeight   =   64.0f;
    float mass        =    0.0f;
    float friction    =    1.0f;
    float restitution =    0.0f;
    // Collider grid resolution; 0 = same as the HeightField. Set this to
    // decimate high-resolution heightmaps (a 4K map is 16M floats = 64MB as
    // a full-res collider; physics rarely needs more than 256-512).
    int   resolution  =    0;
};

// Builds a btHeightfieldTerrainShape from a HeightField and attaches a
// RigidbodyComponent to the owner.  Owns the scaled float grid so Bullet's
// raw-pointer lifetime requirement is satisfied.
class HeightFieldColliderComponent : public Component {
public:
    HeightFieldColliderComponent(
        GameObject*                         owner,
        const std::shared_ptr<HeightField>& heightField,
        const HeightFieldColliderProps&     props = {}
    );
    // Out-of-line so unique_ptr<btCollisionShape> sees the complete type.
    ~HeightFieldColliderComponent() override;

    std::string GetName() const override { return "HeightFieldCollider"; }
    void OnAttach() override {}
    void OnDetach() override {}

    // Refill the scaled grid from the (regenerated) HeightField. The grid is
    // resampled at the collider's own resolution, and Bullet reads the raw
    // grid pointer on every query, so updating the values in place is enough
    // — even if the HeightField's resolution changed.
    void SyncFromHeightField();

private:
    std::shared_ptr<HeightField> _heightField;
    float               _heightScale = 32.0f;
    int                 _width = 0, _depth = 0;  // collider grid resolution
    std::vector<float>  _scaledGrid;   // kept alive for Bullet
    std::unique_ptr<btCollisionShape> _shape;
};

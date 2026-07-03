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

    std::string GetName() const override { return "HeightFieldCollider"; }
    void OnAttach() override {}
    void OnDetach() override {}

    // Refill the scaled grid from the (regenerated) HeightField. Bullet reads
    // the raw grid pointer on every query, so updating the values in place is
    // enough — as long as the resolution didn't change.
    void SyncFromHeightField();

private:
    std::shared_ptr<HeightField> _heightField;
    float               _heightScale = 32.0f;
    std::vector<float>  _scaledGrid;   // kept alive for Bullet
    btCollisionShape*   _shape = nullptr;
};

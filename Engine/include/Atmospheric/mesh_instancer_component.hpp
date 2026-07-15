#pragma once
#include "component.hpp"
#include "globals.hpp"
#include "vertex.hpp"// InstanceData
#include <array>
#include <glm/mat4x4.hpp>
#include <vector>

class GameObject;

// Declares one prototype mesh drawn N times from a single component, so a field
// of trees/rocks/grass costs one frustum test, one sort entry, and one batch
// upload per frame instead of N GameObjects each paying that price. The whole
// cloud is submitted as a single RenderCommand carrying an instance span (see
// RenderCommand::instances); BuildBatches folds it into the instanced batch for
// its (mesh, material) — the same batch scattered MeshRenderers of that mesh
// land in.
//
// v1 scope: one prototype per instancer; per-instance data is the model matrix
// only (no per-instance color/tint yet); culling is whole-cloud (the cloud's
// AABB is frustum-tested as a unit — no per-instance culling / GPU culling).
struct MeshInstancerProps {
    MeshHandle prototype;// the mesh drawn once per instance
    MaterialHandle material;// optional shared override; INVALID → prototype's own material
    std::vector<glm::mat4> localTransforms;// per-instance TRS, relative to the GameObject
};

class MeshInstancerComponent : public Component {
public:
    MeshInstancerComponent(GameObject* gameObject, MeshInstancerProps props);
    ~MeshInstancerComponent() override;

    std::string GetName() const override {
        return "MeshInstancer";
    }

    void OnAttach() override;// registers with GraphicsSubsystem
    void OnDetach() override;// unregisters

    MeshHandle GetMesh() const {
        return _props.prototype;
    }
    // Raw shared-override handle forwarded into RenderCommand::material (INVALID
    // → the submission path resolves the prototype mesh's own material).
    MaterialHandle GetMaterialHandle() const {
        return _props.material;
    }

    size_t InstanceCount() const {
        return _props.localTransforms.size();
    }

    // Replace / edit the instance set. Both mark the cloud dirty so the next
    // WorldInstances()/CloudBounds() recomputes world matrices and the AABB.
    void SetTransforms(std::vector<glm::mat4> localTransforms);
    void UpdateTransform(size_t index, const glm::mat4& localTransform);

    // Called by GraphicsSubsystem during submission. Both lazily rebuild the
    // world-space cache when the instance set changed or the GameObject moved,
    // so a static cloud recomputes at most once. The returned span stays valid
    // until the next rebuild (i.e. for the whole frame).
    const std::vector<InstanceData>& WorldInstances();
    const std::array<glm::vec3, 8>& CloudBounds();

private:
    // Rebuilds _worldInstances (goTransform * local) and the cloud AABB. The
    // AABB sweeps the prototype's local bounding box across every instance, so a
    // frustum test on the 8 corners conservatively covers the whole cloud.
    void _rebuild();
    bool _needsRebuild() const;

    MeshInstancerProps _props;
    std::vector<InstanceData> _worldInstances;
    std::array<glm::vec3, 8> _cloudBounds{};
    // Cached GameObject world transform the cache was built against; a mismatch
    // (or _dirty) triggers a rebuild. The all-zero sentinel forces the first.
    glm::mat4 _cachedGoTransform{ 0.0f };
    bool _dirty = true;
};

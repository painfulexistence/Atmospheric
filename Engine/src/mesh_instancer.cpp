#include "mesh_instancer.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "mesh.hpp"
#include <limits>

MeshInstancer::MeshInstancer(GameObject* gameObject, MeshInstancerProps props) : _props(std::move(props)) {
    this->gameObject = gameObject;
}

MeshInstancer::~MeshInstancer() {
}

void MeshInstancer::OnAttach() {
    if (GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->RegisterInstancer(this);
    }
}

void MeshInstancer::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterInstancer(this);
    }
}

void MeshInstancer::SetTransforms(std::vector<glm::mat4> localTransforms) {
    _props.localTransforms = std::move(localTransforms);
    _dirty = true;
}

void MeshInstancer::UpdateTransform(size_t index, const glm::mat4& localTransform) {
    if (index >= _props.localTransforms.size()) return;
    _props.localTransforms[index] = localTransform;
    _dirty = true;
}

// Exact bit compare — GetTransform() returns identical bits while the object is
// stationary, so a mismatch means it actually moved and the cache is stale.
static bool MatEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (a[c][r] != b[c][r]) return false;
    return true;
}

bool MeshInstancer::_needsRebuild() const {
    if (_dirty) return true;
    glm::mat4 cur = gameObject ? gameObject->GetTransform() : glm::mat4(1.0f);
    return !MatEqual(cur, _cachedGoTransform);
}

void MeshInstancer::_rebuild() {
    const glm::mat4 goTransform = gameObject ? gameObject->GetTransform() : glm::mat4(1.0f);
    _worldInstances.resize(_props.localTransforms.size());

    // Prototype's local AABB. Empty (min == max) when the mesh has no computed
    // bounds; then we bound instance origins directly so the cloud still gets a
    // finite AABB.
    AABB protoBox;
    if (Mesh* mesh = AssetManager::Get().GetMeshPtr(_props.prototype)) {
        protoBox = mesh->GetBounds();
    }
    const bool hasProtoBox = !protoBox.IsEmpty();

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    bool any = false;
    for (size_t i = 0; i < _props.localTransforms.size(); ++i) {
        const glm::mat4 world = goTransform * _props.localTransforms[i];
        _worldInstances[i].modelMatrix = world;
        if (hasProtoBox) {
            const AABB wb = AABB::Transform(protoBox, world);
            mn = glm::min(mn, wb.min);
            mx = glm::max(mx, wb.max);
        } else {
            const glm::vec3 wp = glm::vec3(world[3]);
            mn = glm::min(mn, wp);
            mx = glm::max(mx, wp);
        }
        any = true;
    }
    if (!any) {
        _cloudBounds = {};
    } else {
        _cloudBounds = { mn, mx };
    }

    _cachedGoTransform = goTransform;
    _dirty = false;
}

const std::vector<InstanceData>& MeshInstancer::WorldInstances() {
    if (_needsRebuild()) _rebuild();
    return _worldInstances;
}

const AABB& MeshInstancer::CloudBounds() {
    if (_needsRebuild()) _rebuild();
    return _cloudBounds;
}

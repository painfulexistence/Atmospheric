#include "mesh_instancer_component.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "mesh.hpp"
#include <limits>

MeshInstancerComponent::MeshInstancerComponent(GameObject* gameObject, MeshInstancerProps props)
  : _props(std::move(props)) {
    this->gameObject = gameObject;
}

MeshInstancerComponent::~MeshInstancerComponent() {
}

void MeshInstancerComponent::OnAttach() {
    if (GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->RegisterInstancer(this);
    }
}

void MeshInstancerComponent::OnDetach() {
    if (gameObject && gameObject->GetApp() && GraphicsSubsystem::Get()) {
        GraphicsSubsystem::Get()->UnregisterInstancer(this);
    }
}

void MeshInstancerComponent::SetTransforms(std::vector<glm::mat4> localTransforms) {
    _props.localTransforms = std::move(localTransforms);
    _dirty = true;
}

void MeshInstancerComponent::UpdateTransform(size_t index, const glm::mat4& localTransform) {
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

bool MeshInstancerComponent::_needsRebuild() const {
    if (_dirty) return true;
    glm::mat4 cur = gameObject ? gameObject->GetTransform() : glm::mat4(1.0f);
    return !MatEqual(cur, _cachedGoTransform);
}

void MeshInstancerComponent::_rebuild() {
    const glm::mat4 goTransform = gameObject ? gameObject->GetTransform() : glm::mat4(1.0f);
    _worldInstances.resize(_props.localTransforms.size());

    // Prototype's local bounding box. Degenerate (all-zero) when the mesh has
    // no computed bounds; then we bound instance origins directly so the cloud
    // still gets a finite AABB.
    std::array<glm::vec3, 8> protoBox{};
    bool hasProtoBox = false;
    if (Mesh* mesh = AssetManager::Get().GetMeshPtr(_props.prototype)) {
        protoBox = mesh->GetBoundingBox();
        for (const glm::vec3& c : protoBox) {
            if (c != glm::vec3(0.0f)) {
                hasProtoBox = true;
                break;
            }
        }
    }

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    bool any = false;
    for (size_t i = 0; i < _props.localTransforms.size(); ++i) {
        const glm::mat4 world = goTransform * _props.localTransforms[i];
        _worldInstances[i].modelMatrix = world;
        if (hasProtoBox) {
            for (const glm::vec3& c : protoBox) {
                const glm::vec3 wc = glm::vec3(world * glm::vec4(c, 1.0f));
                mn = glm::min(mn, wc);
                mx = glm::max(mx, wc);
            }
        } else {
            const glm::vec3 wp = glm::vec3(world[3]);
            mn = glm::min(mn, wp);
            mx = glm::max(mx, wp);
        }
        any = true;
    }
    if (!any) {
        mn = glm::vec3(0.0f);
        mx = glm::vec3(0.0f);
    }

    _cloudBounds = {
        glm::vec3(mn.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mn.z), glm::vec3(mx.x, mx.y, mn.z),
        glm::vec3(mn.x, mx.y, mn.z), glm::vec3(mn.x, mn.y, mx.z), glm::vec3(mx.x, mn.y, mx.z),
        glm::vec3(mx.x, mx.y, mx.z), glm::vec3(mn.x, mx.y, mx.z),
    };

    _cachedGoTransform = goTransform;
    _dirty = false;
}

const std::vector<InstanceData>& MeshInstancerComponent::WorldInstances() {
    if (_needsRebuild()) _rebuild();
    return _worldInstances;
}

const std::array<glm::vec3, 8>& MeshInstancerComponent::CloudBounds() {
    if (_needsRebuild()) _rebuild();
    return _cloudBounds;
}

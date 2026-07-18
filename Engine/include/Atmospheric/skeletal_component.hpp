#pragma once
#include "animation_clip.hpp"// SkeletonClipHandle
#include "animator_component.hpp"
#include "globals.hpp"// MeshHandle, SkeletonHandle
#include <glm/mat4x4.hpp>
#include <string>
#include <vector>

class SkinnedMaterial;

struct SkeletalProps {
    WrapMode wrap = WrapMode::Loop;
    float speed = 1.0f;
    bool playing = true;
    float startTime = 0.0f;// seconds
};

// Drives GPU skeletal (skinned) animation for a mesh, on the shared
// AnimatorComponent base. Holds a Skeleton (AssetManager) + one or more
// SkeletonClips (AnimationLibrary). Each Evaluate samples the active clip into
// per-joint local poses (bind-pose fallback), accumulates model-space joint
// matrices down the hierarchy, multiplies by inverse-bind, and uploads the
// resulting skinning-matrix palette as the SkinnedMaterial's bone texture. The
// "skinned" shader then blends the four weighted joint matrices per vertex.
class SkeletalComponent : public AnimatorComponent {
public:
    SkeletalComponent(GameObject* owner, MeshHandle mesh, SkeletonHandle skeleton, const SkeletalProps& props = {});
    ~SkeletalComponent() override;

    std::string GetName() const override {
        return "Skeletal";
    }
    void OnAttach() override;
    void DrawImGui() override;

    void AddClip(SkeletonClipHandle clip);
    bool Play(const std::string& clipName);// imperative; resolves within own clips
    using AnimatorComponent::Play;
    void SetClip(SkeletonClipHandle clip);
    const std::string& GetCurrentClip() const {
        return _activeName;
    }

    SkinnedMaterial* GetMaterial() const {
        return _material;
    }
    float GetDuration() const override;

protected:
    void Evaluate(float time) override;

private:
    void EnsureMaterial();
    void UploadPalette();// (re)uploads _palette as the bone texture

    MeshHandle _mesh;
    SkeletonHandle _skeleton;
    std::vector<SkeletonClipHandle> _clips;
    SkeletonClipHandle _active;
    std::string _activeName;
    SkinnedMaterial* _material = nullptr;// owned by AssetManager
    uint32_t _boneTex = 0;// GfxFactory RGBA32F palette texture

    std::vector<glm::mat4> _model;// scratch: model-space joint matrices
    std::vector<glm::mat4> _palette;// scratch: skinning matrices (model * inverseBind)
};

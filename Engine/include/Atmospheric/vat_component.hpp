#pragma once
#include "animation_clip.hpp"
#include "animator_component.hpp"
#include "globals.hpp"
#include <memory>

class VATMaterial;
class VATClip;

struct VATProps {
    WrapMode wrap = WrapMode::Loop;// end behaviour (Loop wraps, ClampHold holds last frame)
    float speed = 1.0f;// playback rate multiplier (1.0 = clip's authored fps)
    bool playing = true;
    float startTime = 0.0f;// initial playhead in seconds
};

// Drives Vertex Animation Texture playback for a mesh, on the shared
// AnimatorComponent base. The clip lives in the AnimationLibrary (shared across
// instances of the same mesh); this component creates the VATMaterial, binds
// the clip, assigns the material to the mesh, registers a MeshComponent, and —
// via the base's centralized tick — advances the playhead so the opaque pass's
// "vat" shader displaces the vertices. Playhead logic (loop/clamp/speed/pause/
// seek/events) all comes from AnimatorComponent; Evaluate only pushes the
// normalized playhead into the material.
class VATComponent : public AnimatorComponent {
public:
    // Convenience: take ownership of a freshly baked clip and register it in the
    // AnimationLibrary under a generated name, so existing call sites that pass a
    // unique_ptr compile unchanged. The mesh must already be registered.
    VATComponent(GameObject* owner, MeshHandle mesh, std::unique_ptr<VATClip> clip, const VATProps& props = {});
    // Share a clip already registered in the library.
    VATComponent(GameObject* owner, MeshHandle mesh, VATClipHandle clip, const VATProps& props = {});

    std::string GetName() const override {
        return "VAT";
    }
    void OnAttach() override;
    void DrawImGui() override;

    VATClip* GetClip() const;
    VATMaterial* GetMaterial() const {
        return _material;
    }
    float GetDuration() const override;

    // Switch to another library clip (e.g. walk → die), keeping the material.
    void SetClip(VATClipHandle clip);

protected:
    void Evaluate(float time) override;

private:
    void EnsureMaterial();// create the VATMaterial + assign to the mesh once

    MeshHandle _mesh;
    VATClipHandle _clip;
    VATMaterial* _material = nullptr;// owned by AssetManager
};

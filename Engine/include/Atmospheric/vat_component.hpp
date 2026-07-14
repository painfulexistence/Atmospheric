#pragma once
#include "component.hpp"
#include "globals.hpp"
#include "vat.hpp"
#include <memory>

class VATMaterial;

struct VATProps {
    float speed = 1.0f;// playback rate multiplier (1.0 = clip's authored fps)
    bool loop = true;// wrap the playhead at the end vs. hold the last frame
    bool playing = true;
    float startTime = 0.0f;// initial normalized playhead in [0, 1]
};

// Drives Vertex Animation Texture playback for a mesh. The caller supplies a
// baked VATClip (from VATClip::Bake — offline Houdini export or a runtime bake)
// and the mesh whose vertex ordering the clip was baked against; this component
// creates the VATMaterial, binds the clip, assigns the material to the mesh,
// registers a MeshRendererComponent, and advances the playhead every tick so the opaque
// pass's "vat" shader displaces the vertices.
class VATComponent : public Component {
public:
    // Takes ownership of the clip (it owns GL textures freed on component
    // destruction). The mesh must already be registered with AssetManager.
    VATComponent(GameObject* owner, MeshHandle mesh, std::unique_ptr<VATClip> clip, const VATProps& props = {});
    ~VATComponent() = default;

    std::string GetName() const override {
        return "VAT";
    }
    void OnAttach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    VATClip* GetClip() const {
        return _clip.get();
    }
    VATMaterial* GetMaterial() const {
        return _material;
    }
    float GetNormalizedTime() const {
        return _time;
    }

private:
    MeshHandle _mesh;
    std::unique_ptr<VATClip> _clip;
    VATMaterial* _material = nullptr;// owned by AssetManager
    VATProps _props;
    float _time = 0.0f;// normalized playhead in [0, 1]
};

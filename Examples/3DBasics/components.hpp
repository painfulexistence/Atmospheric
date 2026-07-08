#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/mesh.hpp"
#include "Atmospheric/vertex.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <vector>

class RotatorComponent : public Component {
    glm::vec3 _angVel;

public:
    RotatorComponent(GameObject* go, glm::vec3 angVel) : _angVel(angVel) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "RotatorComponent";
    }
    void OnTick(float dt) override {
        gameObject->SetRotation(gameObject->GetRotation() + _angVel * dt);
    }
};

class OscillatorComponent : public Component {
    glm::vec3 _axis, _base;
    float _amp, _freq, _phase, _t = 0;

public:
    OscillatorComponent(GameObject* go, glm::vec3 axis, float amp, float freq, float phase = 0)
      : _axis(axis), _amp(amp), _freq(freq), _phase(phase) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "OscillatorComponent";
    }
    void OnAttach() override {
        _base = gameObject->GetPosition();
    }
    void OnTick(float dt) override {
        _t += dt;
        gameObject->SetPosition(_base + _axis * (std::sin(_t * _freq + _phase) * _amp));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// VAT (Vertex Animation Texture) demo asset.
//
// Real VAT clips come from Houdini's VAT ROP — a cloth/soft-body/fluid sim baked
// to textures. We can't ship a Houdini export in this repo, so this bakes an
// equivalent clip procedurally at runtime: a UV sphere morphing through a
// looping "gooey blob" deformation. The runtime path it exercises (VATClip
// textures → vat.vert vertex fetch → normal pass) is exactly the one a Houdini
// export would drive; only the source of the frames differs.
//
// The baked frames MUST use the same vertex ordering as the mesh, so the mesh
// and the clip are generated together from one shared vertex list here.
struct VATDemoAsset {
    MeshHandle mesh;
    std::unique_ptr<VATClip> clip;
};

inline VATDemoAsset
    BuildBlobVATAsset(const std::string& name, float radius, int division, int frameCount, float frameRate) {
    const int stacks = division;
    const int sectors = division;

    // Base UV sphere (also the frame-0 rest pose). tangent/bitangent are left
    // zero — vat.vert derives a frame when a mesh has none.
    std::vector<Vertex> verts;
    std::vector<uint16_t> tris;
    std::vector<glm::vec3> dirs;// unit direction per vertex, reused every frame
    verts.reserve((static_cast<std::size_t>(stacks) + 1u) * (static_cast<std::size_t>(sectors) + 1u));
    dirs.reserve(verts.capacity());

    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / stacks;
        float phi = v * glm::pi<float>();// 0..π
        for (int j = 0; j <= sectors; ++j) {
            float u = static_cast<float>(j) / sectors;
            float theta = u * glm::two_pi<float>();// 0..2π
            glm::vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            dirs.push_back(dir);
            verts.push_back({ dir * radius, { u, v }, dir });
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            auto a = static_cast<uint16_t>(i * (sectors + 1) + j);
            auto b = static_cast<uint16_t>((i + 1) * (sectors + 1) + j);
            auto c = static_cast<uint16_t>((i + 1) * (sectors + 1) + j + 1);
            auto d = static_cast<uint16_t>(i * (sectors + 1) + j + 1);
            // CCW when viewed from outside (front faces face out), so back-face
            // culling keeps the surface and the smooth-normal cross products
            // below point outward.
            tris.insert(tris.end(), { a, c, b, a, d, c });
        }
    }

    const size_t vertCount = verts.size();

    // Bake each frame's displaced positions, then recompute smooth normals from
    // the deformed triangles so lighting tracks the wobble. Integer time
    // harmonics keep the last frame continuous with the first (seamless loop).
    VATFrameData data;
    data.frameRate = frameRate;
    data.positions.resize(frameCount);
    data.normals.resize(frameCount);

    for (int f = 0; f < frameCount; ++f) {
        float t = glm::two_pi<float>() * static_cast<float>(f) / static_cast<float>(frameCount);
        auto& pos = data.positions[f];
        auto& nrm = data.normals[f];
        pos.resize(vertCount);
        nrm.assign(vertCount, glm::vec3(0.0f));

        for (size_t k = 0; k < vertCount; ++k) {
            const glm::vec3& dir = dirs[k];
            float disp = 0.16f * std::sin(3.0f * dir.x * glm::pi<float>() + t)
                         + 0.10f * std::sin(5.0f * dir.y * glm::pi<float>() + 2.0f * t)
                         + 0.08f * std::sin(7.0f * dir.z * glm::pi<float>() - t);
            pos[k] = dir * (radius + disp);
        }

        // Accumulate face normals into vertices, then normalize (smooth shading).
        for (size_t ti = 0; ti + 2 < tris.size(); ti += 3) {
            uint16_t i0 = tris[ti], i1 = tris[ti + 1], i2 = tris[ti + 2];
            glm::vec3 fn = glm::cross(pos[i1] - pos[i0], pos[i2] - pos[i0]);
            nrm[i0] += fn;
            nrm[i1] += fn;
            nrm[i2] += fn;
        }
        for (auto& n : nrm) {
            float len = glm::length(n);
            n = len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    auto* meshPtr = new Mesh(MeshType::PRIM);
    meshPtr->Initialize(verts, tris);
    MeshHandle handle = AssetManager::Get().CreateMesh(name, meshPtr);

    return { handle, VATClip::Bake(data) };
}

class SpritePulseComponent : public Component {
    float _min, _max, _freq, _phase, _t = 0;

public:
    SpritePulseComponent(GameObject* go, float minA, float maxA, float freq, float phase = 0)
      : _min(minA), _max(maxA), _freq(freq), _phase(phase) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "SpritePulseComponent";
    }
    void OnTick(float dt) override {
        _t += dt;
        auto* s = gameObject->GetComponent<SpriteComponent>();
        if (!s) return;
        float k = 0.5f + 0.5f * std::sin(_t * _freq + _phase);
        glm::vec4 c = s->GetColor();
        c.a = _min + (_max - _min) * k;
        s->SetColor(c);
    }
};

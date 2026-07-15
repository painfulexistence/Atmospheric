#pragma once
#include "Atmospheric.hpp"
#include "Atmospheric/mesh.hpp"
#include "Atmospheric/vertex.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// VAT (Vertex Animation Texture) enemy avatar for the Deathmatch client.
//
// The enemy is rendered as a procedurally-baked VAT blob: a UV sphere morphing
// through a looping "gooey blob" deformation, played back entirely in the
// vertex shader (see the engine's vat.vert / VATClip). Real VAT clips come from
// Houdini's VAT ROP; this bakes an equivalent one at runtime so the example is
// self-contained. The runtime path (VATClip textures → vat.vert vertex fetch →
// normal pass) is identical either way.
//
// The baked frames MUST use the same vertex ordering as the mesh, so the mesh
// and the clip are generated together from one shared vertex list.
// ─────────────────────────────────────────────────────────────────────────────
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

// Metallic preset synthesis (MetalMaps / MakePresetMetalMaps) was removed:
// PBRMaterial's roughnessFactor/metallicFactor scalars replaced the 1x1 solid
// textures — set the factors and diffuse tint directly on the VAT material.

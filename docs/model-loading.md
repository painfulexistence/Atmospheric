# Model & Level Loading

All three import formats ride one line:

```
file ──[ImportPrefab — pure CPU, no GL, off-thread-safe]──▶ Prefab
     ──[Application::Instantiate — main thread]──▶ GameObject subtree
```

A **`Prefab`** (`prefab.hpp`) is the engine's imported, read-only prefab: mesh
batches, decoded images, material definitions, punctual lights, convex
colliders, and a node tree of transforms — plus, for `.map`, every entity's
key/values. **`Instantiate`** uploads the GPU resources and spawns the subtree:
one leaf GameObject per drawable (`MeshRenderer`), one static rigidbody per
collider-carrying node, `LightComponent`s for lights, named empties for point
entities. Scene JSON references it declaratively:

```jsonc
{ "name": "Level", "position": [0,0,0],
  "prefab": "assets/maps/arena.map", "prefabScale": 1.0 }
```

`prefabScale` only affects `.map` (Quake units; default 1/32). The single-handle
`LoadTBMap` / `LoadGLTF` / `LoadUSD` remain as flatten-to-one-mesh wrappers.

## Format coverage

| | `.map` (TrenchBroom/Quake) | `.gltf` / `.glb` | `.usd*` (TinyUSDZ) |
|---|---|---|---|
| Geometry | classic + Valve 220 + `brushDef` faces, `patchDef2/3` Bezier patches | triangle primitives (byteStride-aware) | Tydra-triangulated GeomMeshes |
| Hierarchy | worldspawn→root, brush entities as group nodes, point entities as transform nodes | full node tree (TRS + matrix) | full Xform tree (local matrices) |
| Materials | per-face texture name → engine material or `textures/<name>.*` | full PBR factors + textures | UsdPreviewSurface factors + textures |
| Textures | TrenchBroom `textures/` convention, texel UVs rescaled by real size | embedded + external, decoded at import | embedded (usdz) + external, decoded via Tydra |
| Lights | `light` entities (intensity/_color) | `KHR_lights_punctual` | — (not wired yet) |
| Colliders | one convex hull per brush → static rigidbody | — | — |
| Entities/metadata | **all** key/values preserved (`Prefab::FindEntities`) | — | — |
| Coord fixes | Z-up→Y-up, uniform scale | native Y-up | stage `upAxis` (X/Z→Y) + `metersPerUnit` at root |
| >65535 verts | per-material batches split | primitives chunked | RenderMeshes chunked |
| Extensions | special textures `nodraw/caulk/skip/hint/clip/trigger/origin` filtered (solid where appropriate); `func_group` merged | `KHR_texture_transform` (baked into UVs), `KHR_materials_emissive_strength` | `preserve_texel_bitdepth` + fp32→u8 fallback |
| Known gaps | UVs not pixel-validated against real renderers; `angle` only (no `angles`/`mangle`) | Draco/meshopt, specular-glossiness, skinning | USD lights/cameras/skels; per-GeomSubset materials |

## `.map` notes

A brush is the convex intersection of its face half-spaces; face polygons are
recovered by clipping a large plane quad against the other planes (as qbsp
does). Brush-primitive UVs use GtkRadiant's `ComputeAxisBase` texture-matrix
mapping; patches tessellate at 8×8 per 3×3 control sub-patch with
finite-difference normals. Classic/Valve UVs are authored in texels
(`MeshData::uvInTexels`) and divided by the real texture size at Instantiate
(64 fallback — the Quake default). Every brush contributes a convex collider
(clip/caulk/nodraw included; hint/areaportal/trigger/origin are non-solid).

Gameplay reads entity data *before* instantiating:

```cpp
Prefab level = ImportMapPrefab("assets/maps/arena.map", 1.0f);
for (const PrefabNode* s : level.FindEntities("info_player_start")) {
    glm::vec3 pos = glm::vec3(s->transform[3]);     // engine space
    float yaw = std::stof(s->properties.at("angle"));
}
Instantiate(level, nullptr, "arena");
```

`docs/example-maps/room.map` — walled room + pillar + `light` +
`info_player_start`. Deathmatch's `assets/maps/arena.map` is the in-game use:
plaza, pillar ring, perimeter walls, parallax landmarks, and two spawns —
31 brush colliders, three named materials.

## glTF notes

Materials map onto the engine's PBR maps: factors become 1×1 solid textures
when a slot has no image; `metallicRoughness` feeds both the metallic and
roughness slots (the shader samples its channels); `doubleSided` disables
culling; emissive (× `KHR_materials_emissive_strength`) approximates through
`Material::ambient`. `KHR_texture_transform` on the base color is baked into
UV0 per primitive. `KHR_lights_punctual` lights attach to their nodes.
`LoadGLTF` (single textured mesh) is unchanged.

## USD notes

USD is a first-class format: `AE_USE_TINYUSDZ` defaults **ON** on native (like
`AE_USE_FFMPEG`), so TinyUSDZ is fetched at configure time (FetchContent,
pinned) with no submodule to init. The option stays as an escape hatch — pass
`-DAE_USE_TINYUSDZ=OFF` to skip fetching/compiling it, and web (Emscripten) is
auto-excluded to keep the WASM payload small (import USD offline and ship the
converted asset). When the stub is active `ImportUSDPrefab` warns and returns an
empty prefab. Tydra keeps mesh points local and reports the node tree, so the
Xform hierarchy survives import. Stage `upAxis`/`metersPerUnit` are read from
the **Stage metas** (Tydra's `RenderScene.meta` is not populated from them) and
applied at the prefab root — Z-up/cm scenes like Pixar's Kitchen_set come in
oriented and scaled correctly. `Examples/USDViewer` is the live demo: a
committed `cube.usda` via the scene-JSON `"prefab"` field, plus Kitchen_set via
`scripts/fetchKitchenSet.sh`.

## Verification

Everything CPU-side is execution-verified outside the engine build:

- **`.map`** — 24-check harness: arena regression, Valve 220, brushDef cube
  (64 units → exactly 1 UV tile through the texture matrix), patch Bezier peak
  height exact (8.0), entity key/values and spawn transforms
  (quake `(64,128,24)` → engine `(64,24,-128)`), light color/intensity
  normalization, special-texture filtering (clip cube renders 0 tris, still
  colliders), per-brush collider counts.
- **glTF** — against real tinygltf: node hierarchy + translation exact, 1×1 PNG
  decode, PBR factors, per-node punctual light, KHR_texture_transform baked
  exactly ((1,0) → (0.75,0.5) for offset .25/.5, scale .5), emissive strength ×2.
- **USD** — against real TinyUSDZ: nested Xform `(10,20,30)` lands as that
  node's local transform, UsdPreviewSurface factors, mesh→material binding,
  Z-up/0.01-mpu root transform (maps USD +Z to engine +Y at 0.01), plus
  suzanne.usdc / cube.usdz / texture-cat-plane.usdz (embedded 768² jpg decodes
  to a usable 8-bit image).

The `Instantiate` half (GPU upload, rigidbodies, LightComponents) is
inspection-verified against the engine APIs — the full engine doesn't build in
the authoring environment; validate in-game via the examples.

## Scene format implications

The importers now preserve exactly what a native scene format must keep:
node hierarchy (USD/glTF/map), per-part materials (all three), authored
volumes (brush colliders), and entity metadata (map key/values). glTF shows
how to store a *model*, `.map` an *editable level*, USD a *composed scene* —
the prefab layer is the common denominator all three project into, and the
scene JSON's `"prefab"` field is how scenes reference them.

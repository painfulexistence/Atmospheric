# Model & Level Loading

`AssetManager` now exposes three importers, each backed by a different asset
model. They all funnel into the same runtime primitive — a single flattened
`Mesh` (16-bit indices) referenced by a `MeshHandle` — but the *source* data
models differ enough to be worth comparing, because that comparison is what
should drive the design of a future native scene format.

| API | Format | Backing | Coord space | Hierarchy | Materials |
|-----|--------|---------|-------------|-----------|-----------|
| `LoadGLTF(path)` | `.gltf` / `.glb` | tinygltf (vcpkg) | Y-up | flattened | PBR built from textures |
| `LoadTBMap(path, scale)` | Quake `.map` | hand-written parser | Z-up → Y-up | brush entities (flattened) | ignored (name only) |
| `LoadUSD(path)` | `.usd/.usda/.usdc/.usdz` | TinyUSDZ + Tydra (opt-in) | Y-up | Xform graph (flattened) | not wired yet |

The single-handle `Load*` calls flatten to one mesh, but there is now also a
unified, hierarchy-preserving import line — see
[The unified import line](#the-unified-import-line).

---

## `LoadGLTF`

Already covered by the existing implementation. Concatenates every primitive of
every mesh into one `Mesh`, builds a `Material` from the first primitive's PBR
textures, and handles interleaved buffers / all the GLTF accessor component
types. Vertex count is capped at 65535 (uint16 indices).

## `LoadTBMap` — Quake brush format

```cpp
MeshHandle level = AssetManager::Get().LoadTBMap("docs/example-maps/room.map");
// scale defaults to 1/32 (Quake units → engine units); pass your own if needed
```

A `.map` is a list of entities `{ … }`. A brush entity additionally holds
brushes `{ … }`, and each brush is a set of **face planes** — three points plus
texture info per line. A brush is the convex solid formed by intersecting every
face's half-space, so the loader recovers each face's polygon by seeding a large
quad on the plane and clipping it against all the other planes of the brush
(the same approach id's `qbsp` uses). The result is triangulated as a fan.

Supported:

- Classic idTech2/idTech3 face format (`TEX xoff yoff rot sx sy`) with the
  canonical base-axis UV projection.
- Valve 220 face format (explicit `[ ux uy uz uoff ]` texture axes).
- `//` line comments, quoted key/values.
- Z-up → Y-up conversion via `(x, y, z) → (x, z, -y)` (a proper rotation, so
  winding — and therefore face culling — is preserved).

Not handled (deliberately, for a first cut):

- Point entities (`info_player_start`, lights, …) are parsed as key/values and
  ignored — there is no scene concept to attach them to yet.
- Per-face materials: face texture names are read but not resolved to engine
  materials; the mesh uses the default material.
- Maps whose brush geometry exceeds 65535 vertices are truncated with a warning.

This importer maps naturally onto the engine's existing CSG blockout system
(`csg.hpp`, `level_blockout.hpp`) — a `.map` is essentially the on-disk,
tool-authorable version of what `CSG::Box`/`Union`/`Subtract` build in code.

`docs/example-maps/room.map` is a ready-to-load example: a walled room with a
central pillar (7 brushes).

## `LoadUSD` — Universal Scene Description

Opt-in. Build with `-DAE_USE_TINYUSDZ=ON`; TinyUSDZ is fetched at configure time
(via CMake FetchContent, like SDL3/lua), so there is no submodule to init:

```bash
cmake --preset=dev -DAE_USE_TINYUSDZ=ON
```

When the flag is off, `LoadUSD` logs a warning and returns an invalid handle, so
the baseline build stays small (per the project vision, USD is a heavy dep).

Under the hood it loads the stage with `tinyusdz::LoadUSDFromFile` and runs
TinyUSDZ's **Tydra** `RenderSceneConverter`, which flattens the composed stage
(references/payloads/variants resolved), triangulates, rebuilds a single index
buffer, and synthesizes normals/tangents. The loader then reads Tydra's
`RenderMesh` points/normals/texcoords — coping with both `vertex`- and
`facevarying`-variability attributes — and flattens all meshes into one `Mesh`.

Not handled yet: USD materials/textures (Tydra exposes them; wiring them to
engine `Material` is follow-up work), `upAxis == Z` stages, skeletal animation,
and the 65535-vertex ceiling applies here too.

`docs/example-usd/cube.usda` is a ready-to-load sample (an 8-vertex cube).

---

## Verifying the importers

Both importers have been checked end-to-end outside the full engine build:

- **`LoadTBMap`** — its geometry/UV math (the brace-parser, plane derivation,
  Sutherland-Hodgman clip, Z-up→Y-up transform) was compiled against real GLM
  and run on `docs/example-maps/room.map`, producing 7 brushes → 168 verts / 84
  triangles with engine-space bounds `(-4,-0.5,-4)…(4,4,4)` — exactly the room's
  256-unit footprint scaled by 1/32, with all face normals axis-aligned and
  outward.
- **`LoadUSD`** — the exact TinyUSDZ + Tydra call path was compiled and run on
  `docs/example-usd/cube.usda`, yielding 1 mesh, 8 points, 12 triangles, 8
  per-vertex normals, and zero out-of-range indices (Tydra triangulated the 6
  quads and rebuilt a single index buffer, as expected).

To verify inside the engine on your own machine, call the importer and inspect
the resulting mesh, e.g. in an example's setup:

```cpp
auto& am = AssetManager::Get();
MeshHandle room = am.LoadTBMap("docs/example-maps/room.map");   // any build
MeshHandle cube = am.LoadUSD("docs/example-usd/cube.usda");   // needs AE_USE_TINYUSDZ=ON
// Attach to a GameObject via a MeshComponent and confirm the geometry renders.
```

The loaders log a summary line (`LoadTBMap '…': N brushes, V verts, I indices` /
`LoadUSD '…': N meshes, …`) on success and a `Warn` on failure, so the console
output is the quickest sanity check before anything reaches the screen.

---

## The unified import line

`Load*` returning one flattened `MeshHandle` is the resource-layer shortcut. The
structured path — the one glTF, USD, and `.map` all funnel through — is
`ImportModel` (`model_import.hpp`):

```
file ──[ImportModel — pure CPU, no GL, off-thread-safe]──▶ ModelData
     ──[Application::InstantiateModel — main thread]──▶ GameObject subtree
```

- **`ModelData`** is the reusable "prefab": a flat `std::vector<MeshData>` plus a
  `ModelNode` transform tree that references those meshes by index. No GPU state,
  so `ImportModel` fits the engine's Phase-1 "pure parse" step (it can run off
  the main thread, like `ParseSceneBlueprint`).
- **`InstantiateModel`** is the Phase-2 (main-thread) half shared by every
  format: it uploads each `MeshData` to a `Mesh` (registered as `"<base>#<i>"`)
  and spawns one `GameObject` per node, attaching a `MeshComponent` per mesh and
  recursing into children. The node tree — not a flattened blob — reaches the
  scene, so per-part transforms and the 65535-vertex-per-mesh ceiling both work
  out (a big model is many sub-meshes, each well under the cap).

Scene vs prefab is a role, not a type: a `ModelData` is a prefab (reusable,
instanceable); a scene is the root you load and run. No separate `PrefabBlueprint`
is needed.

### Declaring a model in a scene

An entity in `main.json` can name a model file directly — one field covers every
format `ImportModel` dispatches:

```jsonc
{ "name": "Arena", "position": [0, 0, 0],
  "components": [ { "type": "CameraController3D" } ],
  "model": "assets/maps/arena.map", "modelScale": 1.0 }
```

`ParseEntity` calls `ImportModel` + `InstantiateModel`, parenting the imported
subtree under the entity so the entity's transform positions the whole model.
`modelScale` only affects `.map` (Quake units; default `1/32`). Today
`ImportModel` routes `.map`; `.gltf`/`.usd` importers land next, at which point
the same `"model"` field imports them with no scene-format change.

---

## Implications for the scene format

The reason to have three importers side by side is that each answers a design
question the engine will eventually have to answer for its **own** scene format:

- **glTF** is a *flat asset* model: nodes exist, but the format is really about
  "here is a mesh + a PBR material." It's the right reference for how to
  serialize a single authored model and its materials.
- **Quake `.map`** is a *constructive* model: geometry is defined by convex
  brushes and boolean-ish half-space intersections, authored to be edited by
  hand or in a level editor. It's the right reference for **editable level
  geometry** and lines up with the existing CSG system — a strong hint that the
  scene format should keep brush/CSG volumes as first-class, not just baked
  triangles.
- **USD** is a *composition* model: an Xform hierarchy with references,
  payloads, variants, and layering. It's the right reference for how to compose
  large scenes from reusable pieces and how to express instancing and overrides.

The common limitation across all three importers today — flattening to one mesh
— is precisely the thing a real scene format must *not* do. The concrete
takeaways for the next iteration:

1. **Preserve hierarchy.** Both USD (Xform) and `.map` (entities/brushes) carry
   a node tree the engine currently discards. A scene format wants a node graph
   with transforms, mapping cleanly onto the component/GameObject model in
   `VISION.md`.
2. **Keep sub-meshes and per-part materials.** The uint16, single-material,
   single-mesh flattening is a rendering shortcut, not a scene model. Multiple
   materials per imported asset should survive as separate draw ranges.
3. **Separate authored volumes from baked geometry.** `.map` brushes and CSG
   nodes should round-trip as volumes; triangulation is a build step, not the
   storage format.
4. **Adopt composition where it pays.** USD's references/variants are the proven
   answer to "assemble a big scene from small reusable files" — worth borrowing
   conceptually even if the on-disk format stays custom.

In short: glTF tells us how to store a model, `.map`/CSG tells us how to store
*editable* geometry, and USD tells us how to *compose* a scene. The engine's
scene format should be the union of those three lessons, not a fourth flat mesh
dump.

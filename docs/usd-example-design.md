# Design: a USD scene example (`LoadUSD`)

A proposal for where `LoadUSD` should live as an example, what else the example
needs beyond the model, and how a USD model should interact with the JSON scene
manager. Grounded in how scene loading actually works today (see findings).

## Findings (current state)

- **Scenes are declared in `assets/scenes/main.json`** and parsed by
  `Application::ParseEntity` → `ComponentFactory::Create(type, go, deserializer)`.
  The schema is `{ name, textures, shaders, materials, entities:[ { name,
  position, rotation, scale, components:[ {type, …} ] } ] }`.
- **The ComponentFactory registers**: TransformComponent, SpriteComponent,
  Text2D/3DComponent, CameraComponent, CameraController3D, LightComponent,
  VoxelVolume, VoxelWorld, ShapeRendererComponent, Animator2D, ActionManager.
  **There is no MeshComponent / model component.** So 3D models (`LoadGLTF`,
  now `LoadUSD` / `LoadTBMap`) are attached to GameObjects **in C++ (`OnLoad`)**,
  never from JSON. Deathmatch builds its entire arena in code for this reason.
- **HDR/IBL** already exists: `LoadHDR(...)` → `GraphicsSubsystem::Get()->
  renderer->environmentMap = env` (Deathmatch, main.cpp).
- **`LoadUSD` currently flattens the whole stage into one 16-bit-indexed Mesh.**
  The 65535-vertex cap is fine for a cube but **fatal for a real scene** — Pixar's
  Kitchen_set is hundreds of thousands of verts, so today's `LoadUSD` would
  truncate it to the first ~65k and warn. This is the single most important thing
  to fix before a kitchen scene renders (see "Blocker" below).

## Recommendation in one line

A **standalone example** `Examples/USDScene`, built only when `AE_USE_TINYUSDZ=ON`,
that loads a USD stage through a new **declarative `ModelComponent`** so the whole
scene (camera, lights, sky, model) is described in `main.json` — and upgrade
`LoadUSD` to emit **one engine Mesh per USD mesh** so real scenes aren't capped.

## 1. Where it lives — standalone, flag-gated

USD is opt-in and heavy. Folding it into an existing example would either force
`AE_USE_TINYUSDZ` on everyone or leave dead code. So: a new
`Examples/USDScene/` whose `CMakeLists.txt` does nothing unless
`AE_USE_TINYUSDZ` is set:

```cmake
if(AE_USE_TINYUSDZ)
    add_executable(USDScene main.cpp)
    target_link_libraries(USDScene PRIVATE AtmosphericEngine)
    ae_copy_engine_assets(USDScene)
    ae_copy_game_assets(USDScene "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
```

The default CI matrix never builds it (flag off); a USD-on CI lane can.

## 2. What's in the example besides the model

A model alone reads as a floating greybox. The example should ship a minimal but
complete "viewer" scene:

- **Camera + controller** — `CameraComponent` + `CameraController3D` (the engine's
  unified fly camera) so you can orbit/inspect the model.
- **Lighting** — one directional `LightComponent` for shape, plus an **HDR/IBL
  environment** via `LoadHDR` (reuse Deathmatch's env-map hook) so PBR surfaces
  aren't flat. The sky doubles as a backdrop.
- **Ground** — a large `CreatePlaneMesh` floor (grid texture) so the model sits in
  space rather than floating.
- **On-screen stats** — a `Text2DComponent` overlay showing mesh/vert/triangle
  counts reported by `LoadUSD`, making the import legible.
- **The model** — the USD stage itself (see asset plan).

## 3. How the model interacts with `main.json` — add `ModelComponent`

Today models can't be declared in JSON. The valuable move — and the direct answer
to "how does the model interact with scene management" — is a small, general
`ModelComponent` registered in the factory that dispatches on file extension:

```jsonc
// assets/scenes/main.json
{
  "name": "main",
  "entities": [
    { "name": "Camera", "position": [4, 3, 6],
      "components": [ { "type": "CameraComponent" }, { "type": "CameraController3D" } ] },
    { "name": "Sun", "rotation": [-50, -30, 0],
      "components": [ { "type": "LightComponent", "kind": "directional", "color": [1,1,1] } ] },
    { "name": "Kitchen", "position": [0,0,0], "scale": [1,1,1],
      "components": [ { "type": "ModelComponent", "source": "assets/kitchen.usd" } ] }
  ]
}
```

```cpp
// Application::RegisterComponents(), next to the others:
ComponentFactory::Register("ModelComponent", [](GameObject* o, Deserializer& d) -> Component* {
    std::string src = d.GetString("source", "");
    float scale = d.GetFloat("scale", 1.0f);   // for .map (Quake units)
    auto& am = AssetManager::Get();
    MeshHandle h;
    if (ends_with(src, ".usd") || ends_with(src, ".usda") ||
        ends_with(src, ".usdc") || ends_with(src, ".usdz")) h = am.LoadUSD(src);
    else if (ends_with(src, ".gltf") || ends_with(src, ".glb"))  h = am.LoadGLTF(src);
    else if (ends_with(src, ".map"))                            h = am.LoadTBMap(src, scale);
    return h.IsValid() ? o->AddComponent<MeshComponent>(h) : nullptr;
});
```

Payoff: the *same* `ModelComponent` declares glTF, USD, or `.map` models from
JSON, positioned/rotated/scaled by the entity's transform. `main.json` becomes
the single description of the scene; the USD file supplies geometry. This is also
a concrete step toward the scene-format direction in `model-loading.md`
(hierarchy + per-part material preserved by the format, geometry supplied by
imported assets).

*Interaction model:* the entity's transform positions the imported model in the
world; the USD's **internal** Xform hierarchy is currently baked into the mesh by
Tydra (flattened). Preserving that internal hierarchy as child GameObjects is a
later step (needs the multi-mesh change below first).

## 4. Blocker to fix first: 16-bit index cap

A real kitchen exceeds 65535 verts, so `LoadUSD` must stop flattening to one
Mesh. Two options:

- **(Recommended) One engine Mesh per USD `RenderMesh`.** Tydra already returns a
  `std::vector<RenderMesh>`; emit an engine Mesh per entry (each far under 65k),
  return a small mesh group / parent GameObject with a child per mesh. Bonus: it
  preserves sub-meshes and lets per-mesh materials land later — exactly the
  scene-format direction we want.
- **(Alternative) 32-bit indices.** Widen `Mesh`/index buffers from `uint16_t`
  to `uint32_t`. More invasive (touches buffer upload, RenderMesh, every loader)
  and doesn't improve structure.

Recommend the per-mesh split, exposed as `LoadUSDScene(path) -> std::vector<MeshHandle>`
(or a `ModelHandle` group), with the existing single-mesh `LoadUSD` kept for
trivial assets.

## 5. Asset plan (kitchen.usd + a committed fallback)

- **Showcase:** Pixar **Kitchen_set** (openly licensed USD sample) — a genuine
  composition/reference stress test for TinyUSDZ + Tydra. It's large and
  multi-file, so **don't commit it**: add `scripts/fetch_usd_samples.sh` and
  document the download, or point at an NVIDIA Omniverse sample scene. Expect
  greybox shading until USD materials are wired.
- **Committed smoke asset:** a small self-contained stage so the example builds
  and smoke-tests with no download — either extend `docs/example-usd/cube.usda`
  into a tiny "table + props" `.usda`, or bundle one Apple `.usdz` (single file,
  embedded textures; `LoadUSD` auto-detects the zip). This is what the automated
  smoke test loads.

## 6. Limitations to call out in the example's README

- Greybox: USD materials/textures aren't bound yet (Tydra exposes them; follow-up).
- Flattened hierarchy until the multi-mesh change lands.
- `upAxis == "Z"` stages import rotated (Kitchen_set is Y-up, so fine).

## Suggested build order

1. `LoadUSDScene` (per-mesh) + keep `LoadUSD` — unblocks real scenes.
2. `ModelComponent` in the factory — makes models declarative in `main.json`.
3. `Examples/USDScene` (flag-gated) + committed smoke `.usda` + fetch script for
   Kitchen_set + a smoke test under `AE_USE_TINYUSDZ`.
4. (Later) USD material binding; internal-hierarchy → child GameObjects.

# Scene JSON Format

A scene is a JSON file under `assets/scenes/<name>.json`, loaded by
`Application::GoScene("<name>", …)`. Loading runs in three phases:

1. **`ParseSceneBlueprint`** (pure, off-thread) — parses the JSON into a
   `SceneBlueprint` (no GL calls).
2. **`LoadSceneResources`** (main thread) — uploads textures, compiles shaders,
   creates the named materials.
3. **`InstantiateScene` → `ParseEntity`** (main thread) — walks `entities` and
   builds the GameObject tree, attaching components and instantiating prefabs.

> **Plain JSON only.** Scene files are parsed with `nlohmann::json::parse` with
> no comment support — `//` or `/* */` will fail the parse. The annotated
> snippets below are for reading; real files must be comment-free (see
> `docs/example-scenes/showcase.json` for a clean, parseable copy).

## Top-level keys

| Key | Type | Meaning |
|---|---|---|
| `name` | string | Scene name (default `"Unnamed"`). |
| `textures` | string[] | Texture paths to preload. |
| `shaders` | object | `{ "<name>": { "vert", "frag", "tesc?", "tese?" } }`. |
| `materials` | object | Named materials, referenced by `MeshRenderer.material` and by `.map` face names. |
| `meshes` | string[] | Reserved (parsed, not yet wired — mesh files load via `prefab` today). |
| `entities` | object[] | The scene graph roots (see below). |

## Entity

```jsonc
{
  "name": "Crate",           // GameObject name
  "position": [0, 1, 0],     // world/local translation
  "rotation": [0, 45, 0],    // Euler angles in DEGREES (converted to radians)
  "scale":    [2, 2, 2],     // default [1,1,1]
  "active":   true,          // SetActive (default true)
  "components": [ … ],       // attached Components (see below)
  "prefab": "assets/x.map",  // OR/AND import a model/level under this entity
  "prefabScale": 0.03125,    // .map only (Quake units); default 1/32
  "children": [ … ]          // nested entities (same shape, recursive)
}
```

Every field is optional. An entity with only a `name` + `children` is a useful
empty transform group.

### `components`

Each entry is `{ "type": "<RegisteredName>", …fields }`. Unknown types are
skipped with a warning. The types registered today:

| `type` | Key fields |
|---|---|
| `CameraComponent` | `orthographic`, `fieldOfView`, `aspectRatio`, `nearClip`, `farClip`, `eyeOffset` |
| `CameraController3D` | `moveSpeed`, `lookSpeed`, `slowMultiplier`, `fastMultiplier` |
| `LightComponent` | `lightType` (`Directional`/`Point`/`Spot`/`Area`), `diffuse`, `ambient`, `specular`, `direction`, `attenuation`, `intensity`, `castShadow` |
| `MeshRenderer` | `mesh` **or** `primitive` (+ params), `material` — see below |
| `ShapeRendererComponent` | `color`, `thickness`, `filled`, `radius`, `boxHalfSize`, `layer` |
| `SpriteComponent` | `size`, `pivot`, `color`, `layer`, `flipX`, `flipY`, `zOrder` |
| `Text2DComponent` / `Text3DComponent` | `text`, `fontSize`, `fontName`, alignment |
| `VoxelVolume` / `VoxelWorld` | `seed`, `gridDim`, `voxelSize` |
| `Animator2D`, `ActionManager` | clip / action arrays |

#### `MeshRenderer` — bare drawables

Supply geometry one of two ways:

```jsonc
// A) a primitive, built on demand and deduped by its params:
{ "type": "MeshRenderer", "primitive": "cube",   "size": 2.0,               "material": "Brass" }
{ "type": "MeshRenderer", "primitive": "sphere", "radius": 0.8, "division": 32 }
{ "type": "MeshRenderer", "primitive": "plane",  "width": 1.0,  "height": 1.0 }
{ "type": "MeshRenderer", "primitive": "capsule","radius": 0.5, "height": 3.0 }
{ "type": "MeshRenderer", "primitive": "disc",   "radius": 1.0, "segments": 48 }

// B) a mesh already registered in code (AssetManager::CreateMesh/CreateCubeMesh/…):
{ "type": "MeshRenderer", "mesh": "myRegisteredMesh", "material": "Steel" }
```

`material` (optional) overrides the mesh's own material, looked up by name from
the scene's `materials` block or one created in code. Without geometry the
component is skipped with a warning.

### `prefab` — models & levels

`"prefab": "<path>"` imports a `.map` / `.gltf` / `.glb` / `.usd*` file (via
`ImportPrefab`) and instantiates its whole subtree under this entity, positioned
by the entity's transform. One field carries every format; see
[model-loading.md](model-loading.md) for what each importer produces.
`prefabScale` only affects `.map` (Quake units → engine units; default 1/32).
An entity may carry **both** `components` and a `prefab`.

## Complete example — mesh + entities + glTF + USD + Quake map

The scene below (clean copy: `docs/example-scenes/showcase.json`) has a
fly-camera and a light (**entities/components**), a plane/cube/sphere built from
primitives (**mesh**), and one entity each importing a **glTF** model, a **USD**
scene, and a **Quake `.map`** level:

```jsonc
{
  "name": "showcase",
  "materials": {
    "Concrete": { "diffuse": [0.55, 0.55, 0.58], "shininess": 0.2 },
    "Brass":    { "diffuse": [0.72, 0.55, 0.22], "specular": [0.9, 0.8, 0.5], "shininess": 0.6 }
  },
  "entities": [
    { "name": "MainCamera", "position": [0, 6, 14], "components": [
      { "type": "CameraComponent", "fieldOfView": 60.0, "nearClip": 0.05, "farClip": 300.0 },
      { "type": "CameraController3D", "moveSpeed": 12.0, "lookSpeed": 1.5 }
    ]},
    { "name": "Sun", "components": [
      { "type": "LightComponent", "lightType": "Directional",
        "direction": [-0.4, -1.0, -0.3], "diffuse": [1.0, 0.97, 0.9], "intensity": 1.0 }
    ]},

    // — mesh: primitives with named materials —
    { "name": "Floor", "scale": [40, 1, 40], "components": [
      { "type": "MeshRenderer", "primitive": "plane", "material": "Concrete" }
    ]},
    { "name": "MeshCube", "position": [-4, 1, 0], "rotation": [0, 30, 0], "components": [
      { "type": "MeshRenderer", "primitive": "cube", "size": 2.0, "material": "Brass" }
    ]},
    { "name": "MeshBall", "position": [-4, 3.2, 0], "components": [
      { "type": "MeshRenderer", "primitive": "sphere", "radius": 0.8, "division": 32, "material": "Brass" }
    ]},

    // — glTF model prefab —
    { "name": "Model_GLTF", "prefab": "assets/models/helmet.gltf" },

    // — USD scene prefab —
    { "name": "Scene_USD", "position": [4, 0, 0], "prefab": "assets/usd/kitchen.usdz" },

    // — Quake .map level prefab —
    { "name": "Level_Map", "position": [0, 0, -8],
      "prefab": "assets/maps/arena.map", "prefabScale": 0.03125 }
  ]
}
```

The three `prefab` paths are illustrative — point them at assets that exist in
your project (e.g. Deathmatch's `assets/maps/arena.map`, USDViewer's
`assets/cube.usda`). Coordinate/scale conventions per format are documented in
[model-loading.md](model-loading.md).

# GLTFViewer

Fly-camera viewer for glTF / GLB models, driven by the unified prefab import
line (`ImportPrefab` → `Instantiate`) — the sibling of USDViewer. glTF has no
optional build flag, so this always builds, native and web.

```bash
cmake --preset=ninja-local-vcpkg
cmake --build build --target GLTFViewer
```

### Web (WebAssembly)

```bash
./scripts/buildWasm.sh release   # → build-wasm/release/GLTFViewer/GLTFViewer.html
```

The committed `assets/models/cube.gltf` is preloaded into MEMFS and rendered
from the scene JSON's `"prefab"` field.

## Assets

- **`assets/models/cube.gltf`** (committed, ~2.5 KB) — a self-contained PBR cube
  (brushed copper, geometry inlined as a base64 buffer); always renders.
- **Khronos sample models** (not committed) — the real PBR / transmission tests.
  Fetch them once:

  ```bash
  ./scripts/fetchGltfSamples.sh
  ```

  On the next launch, GLTFViewer imports the first present of `DamagedHelmet`,
  `TransmissionTest`, or `DragonAttenuation` (offset beside the cube) and logs
  the mesh / material / vertex counts.

## Coverage

This is a **static PBR viewer**. Imported: full node hierarchy, PBR materials +
textures, and the `KHR_lights_punctual`, `KHR_texture_transform`,
`KHR_materials_emissive_strength`, and `KHR_materials_transmission` / `_volume` /
`_ior` (data-only) extensions.

**Known gaps** — a model using these still loads, but the feature is skipped:

- Skeletal animation & morph targets (renders the static bind pose)
- Draco / meshopt compression (compressed geometry comes in empty)
- KTX2 / Basis textures (`KHR_texture_basisu`)

So a downloaded animated character or a Draco-compressed export may show nothing
or a static pose; uncompressed static PBR models (DamagedHelmet, most props) are
the target.

Controls: WASD + mouse (engine `CameraController3D`).

# USDViewer

Fly-camera viewer for USD scenes, driven entirely by the unified prefab import
line (`ImportPrefab` → `Instantiate`). USD is a first-class format, so
`AE_USE_TINYUSDZ` is ON by default on native — no extra flag needed. (Pass
`-DAE_USE_TINYUSDZ=OFF` to skip it; web builds exclude it automatically.)

```bash
cmake --preset=ninja-local-vcpkg
cmake --build build --target USDViewer
```

### Web (WebAssembly)

TinyUSDZ compiles to WASM (no threads/SharedArrayBuffer/SIMD needed), so the
viewer runs in the browser out of the box — USD is built by default here too:

```bash
./scripts/buildWasm.sh release   # → build-wasm/release/USDViewer/USDViewer.html
```

The committed `cube.usda` is preloaded into MEMFS and rendered from the scene
JSON's `"prefab"` field. Kitchen_set is native-only (its size and hundreds of
external references aren't a realistic web payload). To trim USD out of the WASM
payload, build with `--no-usd`.

Two assets:

- **`assets/models/cube.usda`** (committed) — declared straight from
  `assets/scenes/main.json` via the `"prefab"` entity field; always renders.
- **Pixar Kitchen_set** (not committed, ~30MB) — the real composition test:
  Z-up, cm units, hundreds of meshes with references. Fetch it once:

  ```bash
  ./scripts/downloadUSDSamples.sh
  ```

  On the next launch, USDViewer imports `assets/models/kitchen/Kitchen_set.usd`
  (upAxis/metersPerUnit are handled by the importer) and logs the mesh /
  material / vertex counts.

Controls: WASD + mouse (engine `CameraController3D`).

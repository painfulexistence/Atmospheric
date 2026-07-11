# USDViewer

Fly-camera viewer for USD scenes, driven entirely by the unified prefab import
line (`ImportPrefab` → `Instantiate`). Built only with `-DAE_USE_TINYUSDZ=ON`.

```bash
cmake --preset=dev -DAE_USE_TINYUSDZ=ON
cmake --build --preset=dev --target USDViewer
```

Two assets:

- **`assets/cube.usda`** (committed) — declared straight from
  `assets/scenes/main.json` via the `"prefab"` entity field; always renders.
- **Pixar Kitchen_set** (not committed, ~30MB) — the real composition test:
  Z-up, cm units, hundreds of meshes with references. Fetch it once:

  ```bash
  ./scripts/fetchKitchenSet.sh
  ```

  On the next launch, USDViewer imports `assets/kitchen/Kitchen_set.usd`
  (upAxis/metersPerUnit are handled by the importer) and logs the mesh /
  material / vertex counts.

Controls: WASD + mouse (engine `CameraController3D`).

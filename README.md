# Atmospheric

[![Native](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-native.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-native.yml)
[![Web](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml)

**Atmospheric** is a cross-platform C++ game engine that handles both 2D and 3D games in a single codebase — with Lua scripting, a hybrid rendering pipeline, and WebAssembly support out of the box.

---

## Why Atmospheric?

Most hobby engines pick a lane: 2D *or* 3D, desktop *or* web. Atmospheric doesn't. You get:

- **2D + 3D in one engine** — sprite batching and PBR lighting share the same scene graph. Build a platformer, a first-person shooter, or something that blurs the line entirely.
- **Lua scripting** — author gameplay logic in Lua without recompiling. Hot-reload friendly for rapid iteration.
- **Web-first thinking** — WebAssembly builds via Emscripten with WebGL 2.0 and experimental WebGPU support. Ship a playable demo with no install required.
- **Cross-platform** — Apple Silicon today, Linux and Windows on the roadmap.

---

## Demos

### Noita-like Falling Sand Simulation *(in progress)*
Pixel-perfect cellular automata running on the GPU. Every pixel is a simulated particle — water flows, fire spreads, sand compresses.

### Time Rewinding
[![Time Rewinding Demo](https://img.youtube.com/vi/6OVBciQAt_A/0.jpg)](https://www.youtube.com/watch?v=6OVBciQAt_A)

### Heightmap Terrain w/ Dynamic Tessellation
[![Terrain Demo](https://img.youtube.com/vi/djCToZGKLkc/0.jpg)](https://www.youtube.com/watch?v=djCToZGKLkc)

### Hello World
![Hello World](.github/assets/Demo_HelloWorld.gif)

---

## Feature Matrix

| Feature | Status |
|---------|--------|
| Forward + Deferred rendering | ✅ |
| PBR texturing | ✅ |
| Point & directional shadow maps | ✅ |
| Post-processing (Bloom, etc.) | ✅ |
| 2D sprite batching | ✅ |
| Heightmap terrain + tessellation | ✅ |
| Lua scripting | ✅ |
| HTML/CSS UI (RmlUi) | ✅ |
| ImGui debug overlays | ✅ |
| 3D physics (Bullet) | ✅ |
| 2D physics | ✅ |
| Job system | ✅ |
| WebAssembly (WebGL 2.0) | ✅ |
| WebGPU | 🚧 WIP |
| Windows / Linux | 🗺️ Planned |

---

## Platform Support

| Platform | Backend | Status |
|----------|---------|--------|
| macOS (Apple Silicon) | OpenGL / Metal | ✅ |
| Web (Emscripten) | WebGL 2.0 | ✅ |
| Web (Emscripten) | WebGPU | 🚧 WIP |
| Linux | OpenGL | 🗺️ Planned |
| Windows | DirectX / OpenGL | 🗺️ Planned |

---

## Quick Start

### Prerequisites
- [CMake](https://cmake.org/download/) 3.20+
- [EMSDK](https://emscripten.org/docs/getting_started/downloads.html) *(optional, for web builds)*

### Clone
```bash
git clone --recurse-submodules https://github.com/painfulexistence/Atmospheric.git
cd Atmospheric
```

### Setup Vcpkg
```bash
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

### Native Build
```bash
cmake --preset=dev
cmake --build --preset=dev
```

### WebAssembly Build
```bash
# WebGL 2.0 (stable)
./scripts/buildWasm.sh release

# WebGPU (experimental)
./scripts/buildWasm.sh release --webgpu
```

At runtime, `GfxFactory` auto-detects `navigator.gpu` and falls back to WebGL 2.0 if WebGPU is unavailable.

---

## Examples

| Example | Description |
|---------|-------------|
| `HelloWorld` | Minimal scene setup |
| `LuaScripting` | Gameplay logic in Lua |
| `MazeFPS` | First-person movement and 3D physics |
| `Physics2D` | 2D rigid body simulation |
| `RPG` | Sprite-based top-down with scene loading |
| `SceneLoader` | CSB scene file loading |
| `VoxelWorld` | Chunk-based voxel terrain |
| `MidnightSkyraiders` | 2D/3D hybrid arcade shooter |

---

## Documentation

See [`docs/`](docs/) for architecture proposals, devlogs, and porting guides.

---

## Documentation

See [`docs/`](docs/) for architecture proposals, devlogs, and porting guides. The website entry point is [`docs/index.html`](docs/index.html).

---

## Starter Repo Recommendations

If you're using Atmospheric as a base for a new game, the minimum useful starting point:

```
YourGame/
  CMakeLists.txt          ← link against AtmosphericEngine target
  vcpkg.json              ← inherit or override dependency list
  CMakePresets.json       ← copy from Atmospheric and adjust paths
  src/
    main.cpp              ← application entry point
  assets/
    shaders/              ← GLSL shaders for your game
    textures/             ← game textures
    scenes/               ← CSB scene files (if using SceneLoader)
  scripts/
    buildWasm.sh          ← copy from Atmospheric for web builds
  .github/
    workflows/
      ci.yml              ← native + wasm build matrix
```

**What to include:**
- `CMakeLists.txt` that uses `add_subdirectory(Atmospheric)` or `find_package`
- `vcpkg.json` with the same dependencies as Atmospheric (or a subset)
- A `CMakePresets.json` with `dev` and `release` presets
- At minimum one example scene or `main.cpp` using `Application`

**What not to include:**
- The engine source itself (use as a submodule or package)
- Build artifacts or generated files

---

## License

[MIT](LICENSE)

# Atmospheric

[![Native](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-native.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-native.yml)
[![Web](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml)
<br />

**Atmospheric** is a cross-platform 3D game engine developed in C++.
The project is a labor of love, acting as my stepping stone to gain a deeper understanding of graphics programming concepts and practices.

### Demo
#### Time Rewinding
[![Time Rewinding Demo](https://img.youtube.com/vi/6OVBciQAt_A/0.jpg)](https://www.youtube.com/watch?v=6OVBciQAt_A)
#### Heightmap Terrain w/ Dynamic Tessellation
[![Time Rewinding Demo](https://img.youtube.com/vi/djCToZGKLkc/0.jpg)](https://www.youtube.com/watch?v=djCToZGKLkc)

### Features
- PBR texturing, point/directional shadows, post-processing effects
- Heightmap terrain with collision and shader-based dynamic tessellation
- Custom key binding system
- Immediate-mode debug UIs for engine subsystems
- Simple job system implementation

### Platforms
- Apple Silicon
- Windows (planned)
- Linux (planned)
- Web via Emscripten

----

## Building
Follow the steps below to build the engine:
1. Install prerequsites:
- [CMake](https://cmake.org/download/) (required)
- [EMSDK](https://emscripten.org/docs/getting_started/downloads.html) (optional)
2. Clone this repository
```
git clone --recurse-submodules https://github.com/painfulexistence/Atmospheric.git
```
3. Setup Vcpkg
```
cd Atmospheric
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```
4. Build the project with CMake
```
cmake --preset=dev
cmake --build --preset=dev
```

### WebAssembly (Emscripten)
We support building WebAssembly targets for browser deployment.

1. **Standard Build (WebGL 2.0 Backend)**:
   ```bash
   ./scripts/buildWasm.sh release
   ```

2. **WebGPU Build (WIP)**:
   You can target WebGPU via Emscripten's Dawn port by adding the `--webgpu` flag:
   ```bash
   ./scripts/buildWasm.sh release --webgpu
   ```
   At runtime, `GfxFactory` will detect browser support. If `navigator.gpu` is available, it initializes the WebGPU pipeline; otherwise, it automatically falls back to WebGL 2.0.


----

## Usage
The engine includes some example projects to help you get started. Please note that the API is still evolving and may change in the future. To get started, you can check out the HelloWorld example.

![demo_helloworld](.github/assets/Demo_HelloWorld.gif)

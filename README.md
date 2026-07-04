# Atmospheric

[![Native](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-desktop.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-desktop.yml)
[![Web](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-web.yml)
[![Android](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-android.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-android.yml)
[![iOS](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-ios.yml/badge.svg)](https://github.com/painfulexistence/Atmospheric/actions/workflows/ci-ios.yml)
<br />

> ⚠️ **On AI-Assisted Development**
>
> This codebase is currently heavily AI-assisted — I won't pretend otherwise. I find AI slop just as grating as you do: generated code that compiles but doesn't reflect intent, architecture that looks plausible but collapses under real requirements. That said, I'm genuinely experimenting with agentic workflows, on my own terms. My honest take: coding AI can meaningfully accelerate development when you have a clear picture of what you're building — and I do. This is a serious engineering project, not an AI toy: I'm actively designing the tooling and pipeline to raise code quality to the standard it deserves — static analysis, formatting enforcement, ownership semantics, the works. The plan is to deslop the codebase incrementally; I'm still engineering the pipeline to do that with the rigor it deserves. Consider this an honest snapshot of that work in progress.
>
> All listed platform targets have been verified by me on real hardware or simulators.

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

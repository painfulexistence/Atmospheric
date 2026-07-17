# Slang shader migration

Status: **pipeline landed, one reference shader ported.** This document is the
plan of record for moving Atmospheric's shaders to a single Slang source per
effect.

## Why Slang

Today every effect is authored twice: GLSL (`.vert`/`.frag`/`.tesc`/`.tese`,
consumed by the OpenGL backend) and WGSL (`.wgsl` files plus WGSL embedded in
`*_wgsl.hpp`, consumed by the WebGPU backend). The two copies drift, and a fix
in one is easy to forget in the other. Slang lets us author each effect once
and generate both.

The engine never exceeds OpenGL 4.1 / WebGL2 feature-wise, which is *helpful*
here: Slang is an offline/build-time translator, so no driver support for
anything new is required — GL 4.1 happily runs the generated GLSL.

## Pipeline (option 1: Slang → SPIR-V → SPIRV-Cross)

```
.slang ──slangc──▶ .spv ──spirv-cross──▶ <name>.<entry>.glsl     (GL 4.1, #version 410)
       │                 └─spirv-cross──▶ <name>.<entry>.es.glsl  (GLSL ES 300, WebGL2)
       └────────slangc──▶ <name>.wgsl                             (WebGPU)
```

Slang's *native* GLSL emitter targets Vulkan semantics (descriptor sets,
explicit `layout(binding=…, set=…)`), which is not guaranteed to feed GL 4.1 /
WebGL2 unmodified. SPIRV-Cross is purpose-built to down-level SPIR-V to legacy
GLSL and ESSL, so we route the GL targets through it. WGSL is emitted by slangc
directly (its WGSL target is mature as of 2026).

Two entry points into the pipeline, kept in lockstep:

- **`cmake/CompileSlang.cmake`** — `atmos_compile_slang()`, gated behind
  `-DAE_USE_SLANG=ON` (OFF by default, so existing builds are untouched). Finds
  `slangc` and `spirv-cross` on PATH.
- **`scripts/compile_slang.py`** — standalone driver for CI / pre-commit / hand
  runs. Reads entry points from `// @slang-entry <name>:<stage>` comments in
  each `.slang`, so sources are self-describing.

```
scripts/compile_slang.py \
    Engine/default_assets/shaders/slang \
    Engine/default_assets/shaders/generated
```

Tools come from vcpkg `atmospheric[slang]` (spirv-cross), the Vulkan SDK, or a
Slang release (slangc). Neither tool ships in this repo.

## The gating dependency: the GL backend must move to UBOs

This is the load-bearing constraint and the reason the migration is staged
rather than a flip of a switch.

The OpenGL backend currently uses **loose uniforms** set by name — there are
**~341** `SetUniform` / `glUniform*` call sites across 9 source files
(`shader.cpp`, `renderer.cpp`, `material.cpp` call paths, `gl_pipeline.cpp`,
`batch_renderer_2d.cpp`, `portal_pass.cpp`, `micro_voxel_pass.cpp`,
`voxel_chunk_pass.cpp`, `particle_subsystem.cpp`, `voxel_gi.cpp`). The GL
backend uses **zero** uniform blocks today.

SPIR-V has no concept of loose uniforms, so SPIRV-Cross always emits GLSL with
**uniform blocks (UBOs)**. Therefore a Slang-generated GLSL shader cannot be a
drop-in for the current renderer: each effect's uniforms become a `std140`
block that must be filled via a `GPUBuffer` and bound with
`glBindBufferBase`/`glBindBufferRange`, replacing the per-name `SetUniform`
calls for that effect.

Practical consequence: **migrate one effect at a time, converting its C++
uniform-set sites to a UBO in the same change**, so each step is compilable and
testable against a real GL context. A big-bang conversion of all shaders +
all 341 sites at once is not reviewable and cannot be verified incrementally.

Recommended per-effect steps:

1. Author `slang/<effect>.slang` (merge the `.vert`/`.frag` pair; use a
   `ConstantBuffer<…>` whose layout matches the existing `.wgsl` block).
2. Generate GLSL/ESSL/WGSL; eyeball the output.
3. Add a `GPUBuffer` for the block; replace that effect's `SetUniform` calls
   with a single buffer upload + bind.
4. Point the GL and WebGPU pipelines at the generated sources.
5. Run the effect on desktop GL, WebGL2, and WebGPU; delete the old sources.

## Reference conversion

`slang/lighting_2d.slang` is the first port — a fullscreen post-process with a
UBO, a texture and a sampler. It replaces `lighting_2d.vert`,
`lighting_2d.frag` and `lighting_2d.wgsl`. Its uniform block is laid out to
match the existing WGSL std140 layout exactly, so it is the template for the
rest. (The old three files are left in place until step 3–5 above are done for
the 2D lighting path.)

## Inventory — 60 shader sources

Legend: **easy** = fullscreen/2D, UBO + textures only; **medium** = forward
geometry with vertex attributes; **complex** = large GI/voxel logic;
**GL-only** = no WGSL target possible.

### Post-process & 2D — easy (start here)
| Effect | GL sources | WGSL | Notes |
|---|---|---|---|
| lighting_2d | lighting_2d.vert/.frag | lighting_2d.wgsl | ✅ **ported** → `slang/lighting_2d.slang` |
| bloom | bloom.vert, bloom_threshold/_downsample/_upsample/_composite.frag | — | 5 passes share bloom.vert; one `.slang` with 5 fragment entries |
| post_composite | post_composite.frag | — | tone-map / composite |
| hdr | hdr.vert | — | fullscreen vert; pair with post_composite |
| noop / flat | noop.frag, flat.frag | — | trivial |
| canvas | canvas.vert/.frag | — | RmlUi/2D canvas |
| quad_2d | — | quad_2d.wgsl | WGSL-only today; give it a GL target via Slang |
| fx_blur, fx_glow | — | fx_blur.wgsl, fx_glow.wgsl | fragment-based (not compute); add GL target |

### Screen-space GI / AO — easy–medium
| Effect | GL sources | Notes |
|---|---|---|
| screen_gtao | screen_gtao.frag | fullscreen |
| screen_ssgi | screen_ssgi.frag | fullscreen |
| screen_ssgi_atrous | screen_ssgi_atrous.frag | à-trous filter |
| screen_ssgi_composite | screen_ssgi_composite.frag | fullscreen |

### Forward geometry — medium
| Effect | GL sources | WGSL | Notes |
|---|---|---|---|
| geometry | geometry.vert/.frag | — | G-buffer |
| lighting | lighting.vert/.frag | — | deferred lighting |
| pbr | tbn.vert (?) + pbr.frag | — | main PBR fragment; confirm its vertex pairing |
| skybox | skybox.vert/.frag | — | |
| sun | sun.vert/.frag | — | |
| water | water.vert/.frag | — | |
| grass | grass.vert/.frag | — | instanced |
| portal | portal.vert/.frag | portal_pass_wgsl.hpp | unify embedded WGSL too |
| depth_simple | depth_simple.vert/.frag | — | shadow pass |
| depth_cubemap | depth_cubemap.vert/.frag | — | point-light shadows |
| debug / simple | debug.vert, simple.vert | — | pair with flat/noop |

### Vertex-animation & particles — medium (special resource models)
| Effect | GL sources | Notes |
|---|---|---|
| vat | vat.vert, vat_depth.vert, vat_depth_cubemap.vert | vertex-animation textures |
| Particle | Particle.vert/.frag | draw |
| ParticleSim | ParticleSim.vert | **transform feedback** — WebGPU has no TF; keep a separate compute/WGSL path, do not force one source |

### Voxel / micro-voxel GI — complex
| Effect | GL sources | Embedded WGSL | Notes |
|---|---|---|---|
| microvoxel | microvoxel.vert/.frag, microvoxel_box.vert | micro_voxel_pass_wgsl.hpp | 16 KB fragment; port late |
| microvoxel_gi | microvoxel_gi.frag | ″ | 12 KB fragment |
| microvoxel_atrous | microvoxel_atrous.frag | ″ | |
| voxel | voxel.vert/.frag | voxel_chunk_pass_wgsl.hpp | |
| renderer_wgsl.hpp | — | renderer_wgsl.hpp (672 lines) | main WebGPU forward path; unify with GLSL last |

### Terrain — GL-only (no WGSL target)
| Effect | GL sources | Notes |
|---|---|---|
| terrain | terrain.vert/.tesc/.tese/.frag | **hardware tessellation** — WGSL has no tessellation stage. Slang emits tesc/tese GLSL for GL 4.1, but there is no WebGPU equivalent; the WebGPU path must keep its own non-tessellated variant. |
| terrain_simple | terrain_simple.vert | non-tessellated fallback |

## Migration order

1. **easy** post-process/2D (bloom, post_composite, fx_*, quad_2d, screen_*) —
   fewest uniforms, exercises the UBO conversion on low-risk passes.
2. **medium** forward geometry (geometry, lighting, pbr, skybox, sun, water,
   grass, portal, depth_*).
3. **special** vat / particles (keep transform-feedback GL-only).
4. **complex** voxel / micro-voxel GI and the embedded-WGSL passes.
5. **terrain** last (GL tessellation stays GL-only).

Each item is one PR: `.slang` source + generated outputs wired in + that
effect's C++ uniform sites converted to a UBO + verified on GL / WebGL2 /
WebGPU.

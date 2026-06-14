// Batch quad renderer — ported from 2d-engine/src/renderer/renderer.ts
// Used by the native Dawn/WebGPU path (AE_USE_DAWN, not yet wired up).
// For the Emscripten WebGPU path, wire up via gpu_render_target.cpp once
// the Dawn submodule is integrated (see cpp-dawn-renderer-eval.md).

struct Uniforms { viewProj: mat4x4<f32> }
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(1) @binding(0) var tex:  texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct Vert {
  @location(0) pos:   vec2<f32>,
  @location(1) uv:    vec2<f32>,
  @location(2) color: vec4<f32>,
  @location(3) flags: vec2<f32>,
}
struct VOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv:    vec2<f32>,
  @location(1) color: vec4<f32>,
  @location(2) flags: vec2<f32>,
}

@vertex fn vs(v: Vert) -> VOut {
  return VOut(
    uni.viewProj * vec4<f32>(v.pos, 0.0, 1.0),
    v.uv, v.color, v.flags
  );
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
  let flags    = u32(in.flags.y);
  let s        = textureSample(tex, samp, in.uv);
  let masked   = vec4<f32>(in.color.rgb, in.color.a * s.a);
  let textured = s * in.color;
  // flag 1 = solid colour (skip texture), flag 2 = mask (use tex alpha only)
  return select(
    select(textured, masked,    (flags & 2u) != 0u),
    in.color,                                        (flags & 1u) != 0u
  );
}

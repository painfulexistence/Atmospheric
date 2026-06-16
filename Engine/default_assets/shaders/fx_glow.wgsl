// Glow / bloom effect (threshold + blur + composite)
// Ported from 2d-engine/src/fx/fx.ts  THRESHOLD_WGSL + COMPOSITE_WGSL
//
// Usage:
//   1. Run threshold pass  (threshold_vs / threshold_fs) → bright-only texture
//   2. Run two-pass blur   (fx_blur.wgsl x2)             → blurred bright texture
//   3. Run composite pass  (composite_vs / composite_fs) → scene + bloom

// ── Threshold ──────────────────────────────────────────────────────────────

@group(0) @binding(0) var src:       texture_2d<f32>;
@group(0) @binding(1) var samp:      sampler;
@group(0) @binding(2) var<uniform>   threshold: f32;

struct VOut { @builtin(position) pos: vec4<f32>, @location(0) uv: vec2<f32> }

@vertex fn threshold_vs(@builtin(vertex_index) vi: u32) -> VOut {
  let x = f32((vi << 1u) & 2u) * 2.0 - 1.0;
  let y = f32(vi & 2u) * -2.0 + 1.0;
  return VOut(vec4<f32>(x, y, 0, 1), vec2<f32>(x * 0.5 + 0.5, 0.5 - y * 0.5));
}

@fragment fn threshold_fs(in: VOut) -> @location(0) vec4<f32> {
  let c   = textureSample(src, samp, in.uv);
  let lum = dot(c.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
  return select(vec4<f32>(0.0), c, lum > threshold);
}

// ── Composite ──────────────────────────────────────────────────────────────

@group(0) @binding(0) var base:            texture_2d<f32>;
@group(0) @binding(1) var bloom:           texture_2d<f32>;
@group(0) @binding(2) var comp_samp:       sampler;
@group(0) @binding(3) var<uniform> strength: f32;

struct CVOut { @builtin(position) pos: vec4<f32>, @location(0) uv: vec2<f32> }

@vertex fn composite_vs(@builtin(vertex_index) vi: u32) -> CVOut {
  let x = f32((vi << 1u) & 2u) * 2.0 - 1.0;
  let y = f32(vi & 2u) * -2.0 + 1.0;
  return CVOut(vec4<f32>(x, y, 0, 1), vec2<f32>(x * 0.5 + 0.5, 0.5 - y * 0.5));
}

@fragment fn composite_fs(in: CVOut) -> @location(0) vec4<f32> {
  let b = textureSample(base,  comp_samp, in.uv);
  let g = textureSample(bloom, comp_samp, in.uv);
  return vec4<f32>(b.rgb + g.rgb * strength, b.a);
}

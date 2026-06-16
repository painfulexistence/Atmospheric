// 2D point-light post-process pass
// Ported from 2d-engine/src/fx/lighting.ts  LIGHTING_WGSL
//
// Pipeline:
//   1. Scene rendered to an offscreen texture
//   2. Full-screen triangle samples the scene, accumulates ambient + point lights
//
// Uniform layout (WebGPU std140):
//   offset  0 : ambient     vec4<f32>   (16 bytes)
//   offset 16 : lightCount  u32         (4 bytes)
//   offset 20 : _pad        u32[3]      (12 bytes)
//   offset 32 : lights[16]              each 48 bytes:
//     +0  pos       vec2<f32>  8
//     +8  _pad      vec2<f32>  8
//     +16 color     vec4<f32> 16
//     +32 radius    f32        4
//     +36 intensity f32        4
//     +40 _pad2     vec2<f32>  8

struct Light {
  pos:       vec2<f32>,
  _pad:      vec2<f32>,
  color:     vec4<f32>,
  radius:    f32,
  intensity: f32,
  _pad2:     vec2<f32>,
}

struct LightingUniforms {
  ambient:    vec4<f32>,
  lightCount: u32,
  _p0: u32, _p1: u32, _p2: u32,
  lights: array<Light, 16>,
}

@group(0) @binding(0) var<uniform> uni:      LightingUniforms;
@group(0) @binding(1) var          sceneTex: texture_2d<f32>;
@group(0) @binding(2) var          samp:     sampler;

struct VOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
}

@vertex
fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  let x = f32((vi << 1u) & 2u) * 2.0 - 1.0;
  let y = f32(vi & 2u) * -2.0 + 1.0;
  var out: VOut;
  out.pos = vec4<f32>(x, y, 0.0, 1.0);
  out.uv  = vec2<f32>(x * 0.5 + 0.5, 0.5 - y * 0.5);
  return out;
}

@fragment
fn fs(in: VOut) -> @location(0) vec4<f32> {
  let scene   = textureSample(sceneTex, samp, in.uv);
  let texSize = vec2<f32>(textureDimensions(sceneTex));
  let pixPos  = in.uv * texSize;

  var lightAccum = uni.ambient.rgb * uni.ambient.a;

  for (var i = 0u; i < uni.lightCount; i++) {
    let l     = uni.lights[i];
    let d     = length(pixPos - l.pos);
    let atten = max(0.0, 1.0 - d / l.radius);
    lightAccum += l.color.rgb * (l.intensity * atten * atten);
  }

  return vec4<f32>(scene.rgb * min(lightAccum, vec3<f32>(1.0)), scene.a);
}

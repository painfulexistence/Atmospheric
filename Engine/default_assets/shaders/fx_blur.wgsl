// Separable Gaussian blur (one pass)
// Ported from 2d-engine/src/fx/fx.ts  BLUR_WGSL
//
// Call with HORIZONTAL=true for horizontal pass, false for vertical.
// Use two passes (H then V) for a full 2D blur.
//
// To select direction at compile time set an override constant or simply
// compile two variants (blur_h.wgsl / blur_v.wgsl).

override HORIZONTAL: bool = true;

@group(0) @binding(0) var src:  texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;

struct VOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
}

@vertex fn vs(@builtin(vertex_index) vi: u32) -> VOut {
  let x = f32((vi << 1u) & 2u) * 2.0 - 1.0;
  let y = f32(vi & 2u) * -2.0 + 1.0;
  var o: VOut;
  o.pos = vec4<f32>(x, y, 0.0, 1.0);
  o.uv  = vec2<f32>(x * 0.5 + 0.5, 0.5 - y * 0.5);
  return o;
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
  let sz   = vec2<f32>(textureDimensions(src));
  let step = select(
    vec2<f32>(0.0, 1.0 / sz.y),
    vec2<f32>(1.0 / sz.x, 0.0),
    HORIZONTAL
  );

  // 9-tap Gaussian weights
  let w = array<f32, 5>(0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
  var col = textureSample(src, samp, in.uv) * w[0];
  for (var i = 1; i < 5; i++) {
    let off = step * f32(i);
    col += textureSample(src, samp, in.uv + off) * w[i];
    col += textureSample(src, samp, in.uv - off) * w[i];
  }
  return col;
}

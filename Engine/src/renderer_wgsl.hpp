#pragma once
// Internal WGSL shader sources for renderer.cpp passes.
// Only #include this file from renderer.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ── ForwardOpaquePass ────────────────────────────────────────────────────────
constexpr uint32_t FWD_DRAW_SLOT_STRIDE  = 256;
constexpr uint64_t FWD_FRAME_UNIFORM_SIZE = 128; // viewProj(64) + cameraPos(16) + lightDir(16) + lightColor(16) + ambient(16)
constexpr uint64_t FWD_DRAW_UNIFORM_SIZE  = 128; // model(64) + diffuse(16) + specular(16) + materialAmbient(16) + shininess+pad(16)

// Simplified port of default_assets/shaders/color.vert + color.frag: a single
// directional light (no shadows, no point lights) and a base-color texture
// only (no normal/AO/roughness/metallic maps) — see ForwardOpaquePass header
// comment. Mirrors VOXEL_WGSL's frame/draw uniform split (voxel_chunk_pass.cpp).
static const char* FORWARD_OPAQUE_WGSL = R"(
struct FrameUniforms {
    viewProj:   mat4x4<f32>,
    cameraPos:  vec4<f32>,
    lightDir:   vec4<f32>,
    lightColor: vec4<f32>,
    ambient:    vec4<f32>,
};
struct DrawUniforms {
    model:           mat4x4<f32>,
    diffuse:         vec4<f32>,
    specular:        vec4<f32>,
    materialAmbient: vec4<f32>,
    shininess:       vec4<f32>,
};
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var base_map: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal:   vec3<f32>,
    @location(2) uv:       vec2<f32>,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>, @location(1) aUV: vec2<f32>, @location(2) aNormal: vec3<f32>) -> VSOut {
    var out: VSOut;
    let world = draw.model * vec4<f32>(aPos, 1.0);
    out.worldPos = world.xyz;
    let normalMat = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz);
    out.normal = normalMat * aNormal;
    out.uv = aUV;
    out.position = frame.viewProj * world;
    return out;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    let norm     = normalize(in.normal);
    let lightDir = normalize(frame.lightDir.xyz);
    let viewDir  = normalize(frame.cameraPos.xyz - in.worldPos);
    let halfDir  = normalize(lightDir + viewDir);

    let diff = max(dot(norm, lightDir), 0.0);
    let spec = pow(max(dot(norm, halfDir), 0.0), max(draw.shininess.x * 128.0, 1.0));

    let texColor = textureSample(base_map, samp, in.uv);
    let albedo   = texColor.rgb * draw.diffuse.rgb;

    let ambient  = draw.materialAmbient.rgb + frame.ambient.rgb * albedo;
    let diffuse  = diff * frame.lightColor.rgb * albedo;
    let specular = spec * frame.lightColor.rgb * draw.specular.rgb;

    return vec4<f32>(ambient + diffuse + specular, texColor.a);
}
)";

// ── PostProcessPass ──────────────────────────────────────────────────────────
// Fullscreen-triangle vertex shader + tonemap fragment shader, ported
// byte-for-byte from default_assets/shaders/hdr.frag (double-gamma-wrapped
// exposure tonemap, with the same optional per-channel chromatic-aberration
// UV offset trick).
static const char* POSTPROCESS_WGSL = R"(
struct Uniforms {
    exposure:   f32,
    caEnabled:  f32,
    caStrength: f32,
    _pad:       f32,
};
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(0) @binding(1) var tex:  texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

struct VOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex fn vs(@builtin(vertex_index) vid: u32) -> VOut {
    let x = f32((vid << 1u) & 2u);
    let y = f32(vid & 2u);
    var out: VOut;
    out.pos = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    out.uv  = vec2<f32>(x, y);
    return out;
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    var hdrColor: vec3<f32>;
    if (uni.caEnabled > 0.5) {
        let du = (in.uv.x - 0.5) * (in.uv.x - 0.5) * uni.caStrength;
        let dv = (in.uv.y - 0.5) * (in.uv.y - 0.5) * uni.caStrength;
        let r = textureSample(tex, samp, vec2<f32>(in.uv.x - 2.0 * du, in.uv.y + 4.0 * dv)).r;
        let g = textureSample(tex, samp, vec2<f32>(in.uv.x + 1.0 * du, in.uv.y - 1.0 * dv)).g;
        let b = textureSample(tex, samp, vec2<f32>(in.uv.x + 5.0 * du, in.uv.y - 3.0 * dv)).b;
        hdrColor = vec3<f32>(r, g, b);
    } else {
        hdrColor = textureSample(tex, samp, in.uv).rgb;
    }
    hdrColor = pow(hdrColor, vec3<f32>(2.2));
    let toneMapped = vec3<f32>(1.0) - exp(-hdrColor * uni.exposure);
    let result = pow(toneMapped, vec3<f32>(1.0 / 2.2));
    return vec4<f32>(result, 1.0);
}
)";

#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

#pragma once
// Internal WGSL shader sources for renderer.cpp passes.
// Only #include this file from renderer.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ── ShadowPass ───────────────────────────────────────────────────────────────
constexpr uint32_t SHADOW_DRAW_SLOT_STRIDE   = 256;
constexpr uint64_t SHADOW_FRAME_UNIFORM_SIZE = 64; // lightVP(64)
constexpr uint64_t SHADOW_DRAW_UNIFORM_SIZE  = 64; // model(64)

// Depth-only shadow-map write for the main directional light. Vertex layout
// matches the Standard 56-byte mesh format used by ForwardOpaquePass; only
// position is consumed. lightVP arrives pre-multiplied with the [-1,1]→[0,1]
// depth fixup (WebGPU clips NDC z outside [0,1], unlike GL).
static const char* SHADOW_WGSL = R"(
struct FrameUniforms { lightVP: mat4x4<f32> };
struct DrawUniforms  { model:   mat4x4<f32> };
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;

@vertex
fn vs(@location(0) aPos: vec3<f32>) -> @builtin(position) vec4<f32> {
    return frame.lightVP * draw.model * vec4<f32>(aPos, 1.0);
}
)";

// ── ForwardOpaquePass ────────────────────────────────────────────────────────
constexpr uint32_t FWD_DRAW_SLOT_STRIDE  = 256;
constexpr uint64_t FWD_FRAME_UNIFORM_SIZE = 192; // viewProj(64) + lightVP(64) + cameraPos(16) + lightDir(16) + lightColor(16) + ambient(16)
constexpr uint64_t FWD_DRAW_UNIFORM_SIZE  = 128; // model(64) + diffuse(16) + specular(16) + materialAmbient(16) + shininess+pad(16)

// Simplified port of default_assets/shaders/color.vert + color.frag: a single
// directional light and a base-color texture only (no normal/AO/roughness/
// metallic maps) — see ForwardOpaquePass header comment. Mirrors VOXEL_WGSL's
// frame/draw uniform split (voxel_chunk_pass.cpp). Shadowing samples the
// directional shadow map written by ShadowPass (group 2) with 3x3 PCF.
static const char* FORWARD_OPAQUE_WGSL = R"(
struct FrameUniforms {
    viewProj:   mat4x4<f32>,
    lightVP:    mat4x4<f32>,
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
@group(2) @binding(0) var shadow_map:  texture_depth_2d;
@group(2) @binding(1) var shadow_samp: sampler_comparison;

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

// 1.0 = fully lit, 0.0 = fully shadowed. Positions outside the shadow
// frustum are treated as lit, matching the GL path's border behaviour.
fn shadowFactor(worldPos: vec3<f32>, nDotL: f32) -> f32 {
    let lp  = frame.lightVP * vec4<f32>(worldPos, 1.0);
    let ndc = lp.xyz / lp.w;
    let uv  = ndc.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);
    if (ndc.z <= 0.0 || ndc.z >= 1.0 ||
        uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    // Bias doubled vs the GL shader's 0.0025: the [-1,1]→[0,1] z fixup halves
    // depth-space derivatives, so the same world-space slope needs 2x bias.
    let bias  = max(0.004 * (1.0 - nDotL), 0.001);
    let texel = 1.0 / vec2<f32>(textureDimensions(shadow_map));
    var lit = 0.0;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            // CompareLevel (not Compare): no implicit derivatives, so it is
            // legal after the non-uniform early-return above.
            lit += textureSampleCompareLevel(shadow_map, shadow_samp,
                                             uv + vec2<f32>(f32(x), f32(y)) * texel,
                                             ndc.z - bias);
        }
    }
    return lit / 9.0;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    let norm     = normalize(in.normal);
    let lightDir = normalize(frame.lightDir.xyz);
    let viewDir  = normalize(frame.cameraPos.xyz - in.worldPos);
    let halfDir  = normalize(lightDir + viewDir);

    let diff = max(dot(norm, lightDir), 0.0);
    let spec = pow(max(dot(norm, halfDir), 0.0), max(draw.shininess.x * 128.0, 1.0));

    // Decode the sRGB-encoded base map to linear before lighting, matching
    // pbr.frag's SurfaceColor (pow 2.2); PostProcess re-encodes after tonemap.
    let texColor = textureSample(base_map, samp, in.uv);
    let albedo   = pow(texColor.rgb, vec3<f32>(2.2)) * draw.diffuse.rgb;

    let shadow   = shadowFactor(in.worldPos, diff);
    let ambient  = draw.materialAmbient.rgb + frame.ambient.rgb * albedo;
    let diffuse  = diff * frame.lightColor.rgb * albedo;
    let specular = spec * frame.lightColor.rgb * draw.specular.rgb;

    return vec4<f32>(ambient + shadow * (diffuse + specular), texColor.a);
}
)";

// ── PostProcessPass ──────────────────────────────────────────────────────────
constexpr uint64_t POST_UNIFORM_SIZE = 32; // exposure, caEnabled, caStrength, effect, time + 3 pad

// Fullscreen-triangle vertex shader + the full set of post effects ported from
// default_assets/shaders/: hdr.frag (default tonemap + chromatic aberration),
// post_crt, post_vhs, post_color_grading, post_posterize, post_sobel,
// post_edges, post_vignette. Selected per frame via uni.effect, matching the
// PostEffect enum order (renderer.hpp). The switch is uniform control flow
// (uni.effect comes from a uniform buffer), so textureSample inside the
// branches is valid WGSL.
static const char* POSTPROCESS_WGSL = R"(
struct Uniforms {
    exposure:   f32,
    caEnabled:  f32,
    caStrength: f32,
    effect:     f32, // PostEffect enum: 0=None 1=CRT 2=VHS 3=ColorGrading 4=Posterize 5=Sobel 6=Edges 7=Vignette
    time:       f32,
    _pad0:      f32,
    _pad1:      f32,
    _pad2:      f32,
};
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(1) @binding(0) var tex:  texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

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

const GAMMA: f32     = 2.2;
const INV_GAMMA: f32 = 1.0 / 2.2;

fn uncharted2(x: vec3<f32>) -> vec3<f32> {
    let A = 0.15; let B = 0.50; let C = 0.10; let D = 0.20; let E = 0.02; let F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}

// hdr.frag: double-gamma-wrapped exposure tonemap + optional per-channel
// chromatic-aberration UV offsets.
fn fxNone(uv: vec2<f32>) -> vec3<f32> {
    var hdrColor: vec3<f32>;
    if (uni.caEnabled > 0.5) {
        let du = (uv.x - 0.5) * (uv.x - 0.5) * uni.caStrength;
        let dv = (uv.y - 0.5) * (uv.y - 0.5) * uni.caStrength;
        let r = textureSample(tex, samp, vec2<f32>(uv.x - 2.0 * du, uv.y + 4.0 * dv)).r;
        let g = textureSample(tex, samp, vec2<f32>(uv.x + 1.0 * du, uv.y - 1.0 * dv)).g;
        let b = textureSample(tex, samp, vec2<f32>(uv.x + 5.0 * du, uv.y - 3.0 * dv)).b;
        hdrColor = vec3<f32>(r, g, b);
    } else {
        hdrColor = textureSample(tex, samp, uv).rgb;
    }
    hdrColor = pow(hdrColor, vec3<f32>(GAMMA));
    let toneMapped = vec3<f32>(1.0) - exp(-hdrColor * uni.exposure);
    return pow(toneMapped, vec3<f32>(INV_GAMMA));
}

fn fxCRT(uvIn: vec2<f32>) -> vec3<f32> {
    var uv = (uvIn - 0.5) * 2.0;
    let offset = abs(uv.yx) * vec2<f32>(0.2, 0.25);
    uv += uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    let inside = f32(uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0);

    var col: vec3<f32>;
    col.r = textureSample(tex, samp, uv + vec2<f32>(0.001, 0.0)).r;
    col.g = textureSample(tex, samp, uv).g;
    col.b = textureSample(tex, samp, uv - vec2<f32>(0.001, 0.0)).b;

    col += sin(uv.y * 800.0 + uni.time * 10.0) * 0.04;

    col  = pow(max(col, vec3<f32>(0.0)), vec3<f32>(GAMMA));
    col *= uni.exposure * 0.66;
    col  = pow(col, vec3<f32>(INV_GAMMA));
    return col * inside;
}

fn vhsOnOff(a: f32, b: f32, c: f32) -> f32 {
    return step(c, sin(uni.time + a * cos(uni.time * b)));
}

fn fxVHS(uvIn: vec2<f32>) -> vec3<f32> {
    // screenDistort
    var uv = uvIn - 0.5;
    uv = uv * 1.2 * (1.0/1.2 + 2.0*uv.x*uv.x*uv.y*uv.y);
    uv += 0.5;
    var look = uv;

    let dy = look.y - (uni.time/4.0 - floor(uni.time/4.0));
    let window = 1.0 / (1.0 + 20.0 * dy * dy);
    look.x += sin(look.y*10.0 + uni.time)/50.0 * vhsOnOff(4.0,4.0,0.3) * (1.0+cos(uni.time*80.0)) * window;
    let vShift = 0.4 * vhsOnOff(2.0,3.0,0.9) *
                 (sin(uni.time)*sin(uni.time*20.0) + (0.5+0.1*sin(uni.time*200.0)*cos(uni.time)));
    look.y = fract(look.y + vShift);

    let vigAmt   = 3.0 + 0.3*sin(uni.time + 5.0*cos(uni.time*5.0));
    let vignette = (1.0 - vigAmt*(uv.y-0.5)*(uv.y-0.5)) *
                   (1.0 - vigAmt*(uv.x-0.5)*(uv.x-0.5));

    var col = textureSample(tex, samp, look).rgb;
    col  = pow(col, vec3<f32>(GAMMA));
    col *= uni.exposure;
    col  = uncharted2(col);
    col *= vignette;
    col *= (12.0 + fract(uv.y*30.0 + uni.time)) / 13.0;
    col  = pow(max(col, vec3<f32>(0.0)), vec3<f32>(INV_GAMMA));
    return col;
}

fn fxColorGrading(uv: vec2<f32>) -> vec3<f32> {
    let c = textureSample(tex, samp, uv).rgb;
    return vec3<f32>(
        dot(c, vec3<f32>(0.393, 0.769, 0.189)),
        dot(c, vec3<f32>(0.349, 0.686, 0.168)),
        dot(c, vec3<f32>(0.272, 0.534, 0.131))
    );
}

fn fxPosterize(uv: vec2<f32>) -> vec3<f32> {
    let c = textureSample(tex, samp, uv).rgb;
    return floor(c * 5.0) / 5.0;
}

fn fxSobel(uv: vec2<f32>) -> vec3<f32> {
    let ts = 1.0 / vec2<f32>(textureDimensions(tex));
    let tl = length(textureSample(tex, samp, uv + vec2<f32>(-ts.x,  ts.y)).rgb);
    let t  = length(textureSample(tex, samp, uv + vec2<f32>( 0.0,   ts.y)).rgb);
    let tr = length(textureSample(tex, samp, uv + vec2<f32>( ts.x,  ts.y)).rgb);
    let l  = length(textureSample(tex, samp, uv + vec2<f32>(-ts.x,  0.0 )).rgb);
    let r  = length(textureSample(tex, samp, uv + vec2<f32>( ts.x,  0.0 )).rgb);
    let bl = length(textureSample(tex, samp, uv + vec2<f32>(-ts.x, -ts.y)).rgb);
    let b  = length(textureSample(tex, samp, uv + vec2<f32>( 0.0,  -ts.y)).rgb);
    let br = length(textureSample(tex, samp, uv + vec2<f32>( ts.x, -ts.y)).rgb);
    let gx = -tl + tr - 2.0*l + 2.0*r - bl + br;
    let gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
    let edge = smoothstep(0.1, 0.5, sqrt(gx*gx + gy*gy));

    var col = textureSample(tex, samp, uv).rgb;
    col  = pow(col, vec3<f32>(GAMMA));
    col *= uni.exposure;
    let edgeStrength = 1.0 - smoothstep(2.0, 4.0, length(col));
    col = mix(col, vec3<f32>(0.1), edge * edgeStrength);
    return pow(col, vec3<f32>(INV_GAMMA));
}

fn fxEdges(uv: vec2<f32>) -> vec3<f32> {
    let ts = 1.0 / vec2<f32>(textureDimensions(tex));
    let h = textureSample(tex, samp, uv + vec2<f32>(ts.x, 0.0)) -
            textureSample(tex, samp, uv - vec2<f32>(ts.x, 0.0));
    let v = textureSample(tex, samp, uv + vec2<f32>(0.0, ts.y)) -
            textureSample(tex, samp, uv - vec2<f32>(0.0, ts.y));
    return sqrt((h * h + v * v).rgb);
}

fn fxVignette(uvIn: vec2<f32>) -> vec3<f32> {
    let uv = uvIn * 2.0 - 1.0;
    let vignette = 1.0 - dot(uv, uv) * 0.7;

    var col = textureSample(tex, samp, uvIn).rgb;
    col  = pow(col, vec3<f32>(GAMMA));
    col *= uni.exposure;
    col  = uncharted2(col);
    col *= vignette;
    return pow(max(col, vec3<f32>(0.0)), vec3<f32>(INV_GAMMA));
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    var result: vec3<f32>;
    switch (i32(uni.effect)) {
        case 1:  { result = fxCRT(in.uv); }
        case 2:  { result = fxVHS(in.uv); }
        case 3:  { result = fxColorGrading(in.uv); }
        case 4:  { result = fxPosterize(in.uv); }
        case 5:  { result = fxSobel(in.uv); }
        case 6:  { result = fxEdges(in.uv); }
        case 7:  { result = fxVignette(in.uv); }
        default: { result = fxNone(in.uv); }
    }
    return vec4<f32>(result, 1.0);
}
)";

#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

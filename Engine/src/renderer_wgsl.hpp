#pragma once
// Internal WGSL shader sources for renderer.cpp passes.
// Only #include this file from renderer.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ── ShadowPass ───────────────────────────────────────────────────────────────
constexpr uint32_t SHADOW_DRAW_SLOT_STRIDE = 256;
constexpr uint64_t SHADOW_FRAME_UNIFORM_SIZE = 64;// lightVP(64)
constexpr uint64_t SHADOW_DRAW_UNIFORM_SIZE = 64;// model(64)

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

// VAT-aware shadow depth: SHADOW_WGSL with the vertex displaced from the VAT
// position texture (group 1, unfilterable rgba32float via textureLoad) so a VAT
// caster's shadow matches its animation. The draw slot is widened by vatParams
// (x=vertCount y=frameCount z=time w=enabled).
constexpr uint64_t SHADOW_VAT_DRAW_UNIFORM_SIZE = 80;// model(64) + vatParams(16)

static const char* SHADOW_VAT_WGSL = R"(
struct FrameUniforms { lightVP: mat4x4<f32> };
struct DrawUniforms  { model: mat4x4<f32>, vatParams: vec4<f32> };
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var vat_position_map: texture_2d<f32>;

@vertex
fn vs(@builtin(vertex_index) vid: u32, @location(0) aPos: vec3<f32>) -> @builtin(position) vec4<f32> {
    var localPos = aPos;
    let vcount  = i32(draw.vatParams.x);
    let fcount  = i32(draw.vatParams.y);
    let vtime   = draw.vatParams.z;
    let enabled = draw.vatParams.w;
    if (enabled > 0.5 && vcount > 0 && fcount > 0) {
        let col = i32(vid);
        let ft  = vtime * f32(max(fcount - 1, 0));
        let f0  = i32(floor(ft));
        let f1  = min(f0 + 1, fcount - 1);
        let fr  = ft - floor(ft);
        let p0 = textureLoad(vat_position_map, vec2<i32>(col, f0), 0).xyz;
        let p1 = textureLoad(vat_position_map, vec2<i32>(col, f1), 0).xyz;
        localPos = mix(p0, p1, fr);
    }
    return frame.lightVP * draw.model * vec4<f32>(localPos, 1.0);
}
)";

// ── ForwardOpaquePass ────────────────────────────────────────────────────────
constexpr uint32_t FWD_DRAW_SLOT_STRIDE = 256;
constexpr uint64_t FWD_FRAME_UNIFORM_SIZE =
    208;// viewProj(64) + lightVP(64) + cameraPos(16) + lightDir(16) + lightColor(16) + ambient(16) + clipPlane(16)
constexpr uint64_t FWD_DRAW_UNIFORM_SIZE =
    128;// model(64) + diffuse(16) + specular(16) + materialAmbient(16) + shininess+pad(16)

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
    // World-space clip plane (n, d) used by PlanarReflectionPass to cut away
    // geometry below the mirror plane; all-zero disables (dot == 0 is kept).
    clipPlane:  vec4<f32>,
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

// Mirror of pbr.frag's Cook-Torrance directional lighting. The ao/roughness/
// metallic maps aren't bound under WebGPU (base map only); the constants
// below stand in for the engine's default map values. Composition matches
// pbr.frag main() exactly: BRDF·lightDiffuse·(1-shadow)·ndl + 0.2·ao·albedo,
// then gamma-ENCODED before writing — the tonemap pass (hdr.frag port)
// expects gamma-encoded input and decodes with pow(2.2) first. Note that
// light.ambient and material ambient/shininess are ignored, as in pbr.frag.
const PI: f32 = 3.1415927;
const SURF_AO: f32        = 1.0;
const SURF_ROUGHNESS: f32 = 0.6;
const SURF_METALLIC: f32  = 0.0;

fn trowbridgeReitzGGX(nh: f32, r: f32) -> f32 {
    let a2   = r * r * r * r;
    let nhr  = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * nhr * nhr);
}

fn smithsSchlickGGX(nv: f32, nl: f32, r: f32) -> f32 {
    let k    = (r + 1.0) * (r + 1.0) / 8.0;
    let ggx1 = nv / (nv * (1.0 - k) + k);
    let ggx2 = nl / (nl * (1.0 - k) + k);
    return ggx1 * ggx2;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    if (dot(frame.clipPlane.xyz, in.worldPos) + frame.clipPlane.w < 0.0) {
        discard;
    }
    let norm     = normalize(in.normal);
    let lightDir = normalize(frame.lightDir.xyz);
    let viewDir  = normalize(frame.cameraPos.xyz - in.worldPos);
    let halfway  = normalize(lightDir + viewDir);

    let nv = clamp(dot(norm, viewDir),  0.0, 1.0);
    let nl = clamp(dot(norm, lightDir), 0.0, 1.0);
    let nh = clamp(dot(norm, halfway),  0.0, 1.0);
    let vh = clamp(dot(viewDir, halfway), 0.0, 1.0);

    // SurfaceColor: sRGB base map decoded to linear, tinted by material diffuse.
    let texColor = textureSample(base_map, samp, in.uv);
    let albedo   = pow(texColor.rgb, vec3<f32>(2.2)) * draw.diffuse.rgb;

    let r  = SURF_ROUGHNESS + 0.01;
    let d  = trowbridgeReitzGGX(nh, r);
    let g  = smithsSchlickGGX(nv, nl, r);
    let f0 = mix(vec3<f32>(0.04), albedo, SURF_METALLIC);
    let f  = f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - vh, 5.0);

    let specular = d * f * g / max(4.0 * nv * nl, 0.0001);
    let kd       = (1.0 - SURF_METALLIC) * (vec3<f32>(1.0) - f);
    let brdf     = kd * albedo / PI + specular;

    let shadow   = shadowFactor(in.worldPos, nl); // 1 = lit
    let radiance = frame.lightColor.rgb * shadow;

    var result = brdf * radiance * nl;
    result    += vec3<f32>(0.2) * SURF_AO * albedo;

    return vec4<f32>(pow(result, vec3<f32>(1.0 / 2.2)), texColor.a);
}
)";

// Vertex Animation Texture forward pass — FORWARD_OPAQUE_WGSL's fragment stage
// verbatim, with a vertex stage that displaces each vertex from the VAT
// position/normal textures (group 3, unfilterable rgba32float sampled with
// textureLoad — no sampler, no filtering). The animation frame data is packed
// into the shared DrawUniforms' spare slot (shininess → vatParams), so the VAT
// pipeline reuses group 0/1/2 (frame+draw uniforms, base map, shadow map) and
// only adds group 3. Frames are blended in-shader (two textureLoads + mix)
// since rgba32float can't be linearly filtered.
static const char* VAT_WGSL = R"(
struct FrameUniforms {
    viewProj:   mat4x4<f32>,
    lightVP:    mat4x4<f32>,
    cameraPos:  vec4<f32>,
    lightDir:   vec4<f32>,
    lightColor: vec4<f32>,
    ambient:    vec4<f32>,
    clipPlane:  vec4<f32>,// matches FORWARD_OPAQUE_WGSL (208-byte frame uniform)
};
struct DrawUniforms {
    model:           mat4x4<f32>,
    diffuse:         vec4<f32>,
    specular:        vec4<f32>,
    materialAmbient: vec4<f32>,
    vatParams:       vec4<f32>, // x=vertCount y=frameCount z=time w=enabled
};
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var base_map: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;
@group(2) @binding(0) var shadow_map:  texture_depth_2d;
@group(2) @binding(1) var shadow_samp: sampler_comparison;
@group(3) @binding(0) var vat_position_map: texture_2d<f32>;
@group(3) @binding(1) var vat_normal_map:   texture_2d<f32>;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal:   vec3<f32>,
    @location(2) uv:       vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) vid: u32,
      @location(0) aPos: vec3<f32>, @location(1) aUV: vec2<f32>, @location(2) aNormal: vec3<f32>) -> VSOut {
    var localPos  = aPos;
    var localNorm = aNormal;

    let vcount  = i32(draw.vatParams.x);
    let fcount  = i32(draw.vatParams.y);
    let vtime   = draw.vatParams.z;
    let enabled = draw.vatParams.w;
    if (enabled > 0.5 && vcount > 0 && fcount > 0) {
        let col = i32(vid);
        let ft  = vtime * f32(max(fcount - 1, 0));
        let f0  = i32(floor(ft));
        let f1  = min(f0 + 1, fcount - 1);
        let fr  = ft - floor(ft);
        let p0 = textureLoad(vat_position_map, vec2<i32>(col, f0), 0).xyz;
        let p1 = textureLoad(vat_position_map, vec2<i32>(col, f1), 0).xyz;
        let n0 = textureLoad(vat_normal_map,   vec2<i32>(col, f0), 0).xyz;
        let n1 = textureLoad(vat_normal_map,   vec2<i32>(col, f1), 0).xyz;
        localPos  = mix(p0, p1, fr);
        localNorm = normalize(mix(n0, n1, fr));
    }

    var out: VSOut;
    let world = draw.model * vec4<f32>(localPos, 1.0);
    out.worldPos = world.xyz;
    let normalMat = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz);
    out.normal = normalMat * localNorm;
    out.uv = aUV;
    out.position = frame.viewProj * world;
    return out;
}

fn shadowFactor(worldPos: vec3<f32>, nDotL: f32) -> f32 {
    let lp  = frame.lightVP * vec4<f32>(worldPos, 1.0);
    let ndc = lp.xyz / lp.w;
    let uv  = ndc.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);
    if (ndc.z <= 0.0 || ndc.z >= 1.0 ||
        uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    let bias  = max(0.004 * (1.0 - nDotL), 0.001);
    let texel = 1.0 / vec2<f32>(textureDimensions(shadow_map));
    var lit = 0.0;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            lit += textureSampleCompareLevel(shadow_map, shadow_samp,
                                             uv + vec2<f32>(f32(x), f32(y)) * texel,
                                             ndc.z - bias);
        }
    }
    return lit / 9.0;
}

const PI: f32 = 3.1415927;
const SURF_AO: f32        = 1.0;
const SURF_ROUGHNESS: f32 = 0.6;
const SURF_METALLIC: f32  = 0.0;

fn trowbridgeReitzGGX(nh: f32, r: f32) -> f32 {
    let a2   = r * r * r * r;
    let nhr  = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * nhr * nhr);
}

fn smithsSchlickGGX(nv: f32, nl: f32, r: f32) -> f32 {
    let k    = (r + 1.0) * (r + 1.0) / 8.0;
    let ggx1 = nv / (nv * (1.0 - k) + k);
    let ggx2 = nl / (nl * (1.0 - k) + k);
    return ggx1 * ggx2;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    if (dot(frame.clipPlane.xyz, in.worldPos) + frame.clipPlane.w < 0.0) {
        discard;
    }
    let norm     = normalize(in.normal);
    let lightDir = normalize(frame.lightDir.xyz);
    let viewDir  = normalize(frame.cameraPos.xyz - in.worldPos);
    let halfway  = normalize(lightDir + viewDir);

    let nv = clamp(dot(norm, viewDir),  0.0, 1.0);
    let nl = clamp(dot(norm, lightDir), 0.0, 1.0);
    let nh = clamp(dot(norm, halfway),  0.0, 1.0);
    let vh = clamp(dot(viewDir, halfway), 0.0, 1.0);

    let texColor = textureSample(base_map, samp, in.uv);
    let albedo   = pow(texColor.rgb, vec3<f32>(2.2)) * draw.diffuse.rgb;

    let r  = SURF_ROUGHNESS + 0.01;
    let d  = trowbridgeReitzGGX(nh, r);
    let g  = smithsSchlickGGX(nv, nl, r);
    let f0 = mix(vec3<f32>(0.04), albedo, SURF_METALLIC);
    let f  = f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - vh, 5.0);

    let specular = d * f * g / max(4.0 * nv * nl, 0.0001);
    let kd       = (1.0 - SURF_METALLIC) * (vec3<f32>(1.0) - f);
    let brdf     = kd * albedo / PI + specular;

    let shadow   = shadowFactor(in.worldPos, nl);
    let radiance = frame.lightColor.rgb * shadow;

    var result = brdf * radiance * nl;
    result    += vec3<f32>(0.2) * SURF_AO * albedo;

    return vec4<f32>(pow(result, vec3<f32>(1.0 / 2.2)), texColor.a);
}
)";

// Non-tessellated terrain, mirroring the GLES/WebGL2 fallback pair
// terrain_simple.vert + terrain.frag (palette path — the Terrain example uses
// no color/splat/layer maps). The heightmap is r16float, the same GL_R16F
// format the WebGL2 path samples (GfxFactory::UploadTextureR16F); r16float is
// filterable core WebGPU. Shares group 0 (frame + dynamic draw slot) and
// group 1 (texture + sampler) layouts with FORWARD_OPAQUE_WGSL so the pass
// reuses _uniformBG and _getOrCreateTexBG.
// Terrain draw slots pack params at the draw-uniform offset 64:
//   params = (height_scale, world_size, palette_index, unused)
static const char* TERRAIN_WGSL = R"(
struct FrameUniforms {
    viewProj:   mat4x4<f32>,
    lightVP:    mat4x4<f32>,
    cameraPos:  vec4<f32>,
    lightDir:   vec4<f32>,
    lightColor: vec4<f32>,
    ambient:    vec4<f32>,
    // World-space clip plane (n, d) used by PlanarReflectionPass to cut away
    // geometry below the mirror plane; all-zero disables (dot == 0 is kept).
    clipPlane:  vec4<f32>,
};
struct DrawUniforms {
    model:  mat4x4<f32>,
    params: vec4<f32>, // x=height_scale y=world_size z=palette_index w=unused
};
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var height_map: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

// textureLod(height_map, uv, 0.0).r, as in terrain_simple.vert. Coords are
// clamped in-shader because the shared sampler uses Repeat addressing (base
// maps want tiling; the GL heightmap texture uses CLAMP_TO_EDGE).
fn readHeight(uv: vec2<f32>) -> f32 {
    return textureSampleLevel(height_map, samp,
                              clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).r;
}

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) uv:       vec2<f32>,
    @location(2) height:   f32,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>, @location(1) aUV: vec2<f32>) -> VSOut {
    var out: VSOut;
    let h = readHeight(aUV);
    var pos = aPos;
    pos.y += h * draw.params.x;
    let world = draw.model * vec4<f32>(pos, 1.0);
    out.worldPos = world.xyz;
    out.uv       = aUV;
    out.height   = h;
    out.position = frame.viewProj * world;
    return out;
}

// Cosine palette a + b*cos(2π(c*t + d)); constants copied from terrain.frag
// (which shares them with voxel.frag). Index 0 is the legacy warm pink/gold.
fn palette(t: f32, idx: i32) -> vec3<f32> {
    var a: vec3<f32>; var b: vec3<f32>; var c: vec3<f32>; var d: vec3<f32>;
    switch (idx) {
        case 1: { // cool blue/purple
            a = vec3<f32>(0.288, 0.303, 0.466); b = vec3<f32>(0.806, 0.664, 0.998);
            c = vec3<f32>(1.253, 0.992, 1.569); d = vec3<f32>(3.379, 3.574, 3.026);
        }
        case 2: { // earthy green
            a = vec3<f32>(0.420, 0.696, 0.625); b = vec3<f32>(0.791, 0.182, 0.271);
            c = vec3<f32>(0.368, 0.650, 0.103); d = vec3<f32>(0.913, 3.624, 0.320);
        }
        case 3: { // forest
            a = vec3<f32>(0.427, 0.346, 0.372); b = vec3<f32>(0.288, 0.918, 0.336);
            c = vec3<f32>(0.635, 1.136, 0.404); d = vec3<f32>(1.893, 0.663, 1.910);
        }
        case 4: { // soft cool blue-grey
            a = vec3<f32>(0.746, 0.815, 0.846); b = vec3<f32>(0.195, 0.283, 0.187);
            c = vec3<f32>(1.093, 1.417, 1.405); d = vec3<f32>(5.435, 2.400, 5.741);
        }
        case 5: { // vivid mint/coral
            a = vec3<f32>(0.686, 0.933, 0.933); b = vec3<f32>(0.957, 0.643, 0.957);
            c = vec3<f32>(0.867, 0.627, 0.867); d = vec3<f32>(1.961, 2.871, 1.702);
        }
        default: { // warm pink/gold (legacy terrain default)
            a = vec3<f32>(0.800, 0.150, 0.560); b = vec3<f32>(0.610, 0.300, 0.120);
            c = vec3<f32>(0.640, 0.100, 0.590); d = vec3<f32>(0.380, 0.860, 0.470);
        }
    }
    return a + b * cos(6.28318 * (c * t + d));
}

// World-space normal from heightmap central differences (terrain.frag's
// HeightmapNormal). Mesh UV maps u -> +X and v -> +Z across world_size, so
// the derivation matches the displaced geometry exactly.
fn heightmapNormal(uv: vec2<f32>, heightScale: f32, worldSize: f32) -> vec3<f32> {
    let ts = 1.0 / vec2<f32>(textureDimensions(height_map));
    let hl = readHeight(uv - vec2<f32>(ts.x, 0.0));
    let hr = readHeight(uv + vec2<f32>(ts.x, 0.0));
    let hd = readHeight(uv - vec2<f32>(0.0, ts.y));
    let hu = readHeight(uv + vec2<f32>(0.0, ts.y));
    let dhdx = (hr - hl) * heightScale / (2.0 * ts.x * worldSize);
    let dhdz = (hu - hd) * heightScale / (2.0 * ts.y * worldSize);
    return normalize(vec3<f32>(-dhdx, 1.0, -dhdz));
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    if (dot(frame.clipPlane.xyz, in.worldPos) + frame.clipPlane.w < 0.0) {
        discard;
    }
    let n        = heightmapNormal(in.uv, draw.params.x, draw.params.y);
    // The cosine palette can dip below zero per channel; clamp it or the
    // pow() below is NaN, and one NaN pixel poisons the whole frame through
    // the bloom downsample chain (GLSL pow on negatives is undefined too,
    // but WGSL/Dawn reliably produces NaN here).
    let albedo   = max(palette(in.height, i32(draw.params.z + 0.5)), vec3<f32>(0.0));
    let lightDir = normalize(frame.lightDir.xyz);
    let ndl      = clamp(dot(n, lightDir), 0.0, 1.0);
    var lit = albedo * frame.lightColor.rgb * ndl;
    lit += vec3<f32>(0.2) * albedo; // fixed ambient term, matching terrain.frag
    // Gamma-encode: the tonemap pass expects gamma-encoded input.
    return vec4<f32>(pow(lit, vec3<f32>(1.0 / 2.2)), 1.0);
}
)";

// ── PostProcessPass ──────────────────────────────────────────────────────────
constexpr uint64_t POST_UNIFORM_SIZE =
    48;// exposure,caEn,caStr,time + crt,vhs,grading,posterize + sobel,edges,vignette,pad

// Fullscreen-triangle vertex shader + the full set of post effects ported from
// default_assets/shaders/: hdr.frag (default tonemap + chromatic aberration),
// post_crt, post_vhs, post_color_grading, post_posterize, post_sobel,
// post_edges, post_vignette. Selected per frame via uni.effect, matching the
// PostEffect enum order (renderer.hpp). The switch is uniform control flow
// (uni.effect comes from a uniform buffer), so textureSample inside the
// branches is valid WGSL.
static const char* POSTPROCESS_WGSL = R"(
// Unified post-process stack — faithful WGSL port of
// default_assets/shaders/post_composite.frag. Every effect is an independent
// toggle (0/1 float) applied in the same fixed order as the GL shader:
//   VHS distort -> CRT barrel -> CA/CRT channel fetch -> gamma -> Sobel(HDR)
//   -> tonemap -> Edges -> grading -> posterize -> vignette -> VHS vig/stripe
//   -> CRT scanline -> gamma encode. Toggles come from a uniform buffer, so
//   the branches are uniform control flow (textureSample inside them is legal).
struct Uniforms {
    exposure:   f32,
    caEnabled:  f32,
    caStrength: f32,
    time:       f32,
    crt:        f32,
    vhs:        f32,
    grading:    f32,
    posterize:  f32,
    sobel:      f32,
    edges:      f32,
    vignette:   f32,
    _pad:       f32,
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

fn onOff(a: f32, b: f32, c: f32) -> f32 {
    return step(c, sin(uni.time + a * cos(uni.time * b)));
}

fn vhsScreenDistort(uvIn: vec2<f32>) -> vec2<f32> {
    var uv = uvIn - 0.5;
    uv = uv * 1.2 * (1.0/1.2 + 2.0*uv.x*uv.x*uv.y*uv.y);
    return uv + 0.5;
}

fn vhsTracking(uvIn: vec2<f32>) -> vec2<f32> {
    var look = uvIn;
    let m = uni.time/4.0 - floor(uni.time/4.0);
    let window = 1.0 / (1.0 + 20.0*(look.y - m)*(look.y - m));
    look.x += sin(look.y*10.0 + uni.time)/50.0 * onOff(4.0,4.0,0.3) * (1.0+cos(uni.time*80.0)) * window;
    let vShift = 0.4 * onOff(2.0,3.0,0.9) *
                 (sin(uni.time)*sin(uni.time*20.0) + (0.5+0.1*sin(uni.time*200.0)*cos(uni.time)));
    look.y = fract(look.y + vShift);
    return look;
}

fn sobelMagnitude(uv: vec2<f32>) -> f32 {
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
    return sqrt(gx*gx + gy*gy);
}

fn edgeGradient(uv: vec2<f32>) -> vec3<f32> {
    let ts = 1.0 / vec2<f32>(textureDimensions(tex));
    let h = textureSample(tex, samp, uv + vec2<f32>(ts.x, 0.0)) -
            textureSample(tex, samp, uv - vec2<f32>(ts.x, 0.0));
    let v = textureSample(tex, samp, uv + vec2<f32>(0.0, ts.y)) -
            textureSample(tex, samp, uv - vec2<f32>(0.0, ts.y));
    return sqrt((h * h + v * v).rgb);
}

fn sepia(c: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(
        dot(c, vec3<f32>(0.393, 0.769, 0.189)),
        dot(c, vec3<f32>(0.349, 0.686, 0.168)),
        dot(c, vec3<f32>(0.272, 0.534, 0.131))
    );
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    var uv = in.uv;

    // VHS: screen curvature + tracking wobble
    var vhsVignette = 1.0;
    var vhsStripe   = 1.0;
    if (uni.vhs > 0.5) {
        uv = vhsScreenDistort(uv);
        let vigAmt = 3.0 + 0.3*sin(uni.time + 5.0*cos(uni.time*5.0));
        vhsVignette = (1.0 - vigAmt*(uv.y-0.5)*(uv.y-0.5)) *
                      (1.0 - vigAmt*(uv.x-0.5)*(uv.x-0.5));
        vhsStripe   = (12.0 + fract(uv.y*30.0 + uni.time)) / 13.0;
        uv = vhsTracking(uv);
    }

    // CRT barrel distortion
    var offScreen = false;
    if (uni.crt > 0.5) {
        var c = (uv - 0.5) * 2.0;
        let offset = abs(c.yx) * vec2<f32>(0.2, 0.25);
        c += c * offset * offset;
        uv = c * 0.5 + 0.5;
        offScreen = uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0;
    }

    // Channel fetch (CA + CRT phosphor share the 3-tap path)
    var col: vec3<f32>;
    if (uni.caEnabled > 0.5 || uni.crt > 0.5) {
        var rOff = vec2<f32>(0.0);
        var gOff = vec2<f32>(0.0);
        var bOff = vec2<f32>(0.0);
        if (uni.caEnabled > 0.5) {
            let du = (uv.x - 0.5) * (uv.x - 0.5) * uni.caStrength;
            let dv = (uv.y - 0.5) * (uv.y - 0.5) * uni.caStrength;
            rOff += vec2<f32>(-2.0 * du,  4.0 * dv);
            gOff += vec2<f32>( 1.0 * du, -1.0 * dv);
            bOff += vec2<f32>( 5.0 * du, -3.0 * dv);
        }
        if (uni.crt > 0.5) {
            rOff += vec2<f32>( 0.001, 0.0);
            bOff += vec2<f32>(-0.001, 0.0);
        }
        col = vec3<f32>(
            textureSample(tex, samp, uv + rOff).r,
            textureSample(tex, samp, uv + gOff).g,
            textureSample(tex, samp, uv + bOff).b);
    } else {
        col = textureSample(tex, samp, uv).rgb;
    }

    col = pow(col, vec3<f32>(GAMMA));

    // Sobel in HDR domain
    if (uni.sobel > 0.5) {
        let brightness    = length(col * uni.exposure);
        let edge_strength = 1.0 - smoothstep(2.0, 4.0, brightness);
        let edge          = smoothstep(0.1, 0.5, sobelMagnitude(uv));
        col = mix(col, vec3<f32>(0.1), edge * edge_strength);
    }

    var ldr = vec3<f32>(1.0) - exp(-col * uni.exposure);

    if (uni.edges > 0.5)     { ldr = edgeGradient(uv); }
    if (uni.grading > 0.5)   { ldr = sepia(ldr); }
    if (uni.posterize > 0.5) { ldr = floor(ldr * 5.0) / 5.0; }
    if (uni.vignette > 0.5) {
        let c = in.uv * 2.0 - 1.0;
        ldr *= 1.0 - dot(c, c) * 0.7;
    }
    ldr *= vhsVignette * vhsStripe;
    if (uni.crt > 0.5) { ldr += sin(uv.y*800.0 + uni.time*10.0) * 0.04; }

    ldr = pow(max(ldr, vec3<f32>(0.0)), vec3<f32>(INV_GAMMA));
    if (offScreen) { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }
    return vec4<f32>(ldr, 1.0);
}
)";

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

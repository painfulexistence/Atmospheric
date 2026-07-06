#pragma once
// Internal WGSL shader sources for voxel_chunk_pass.cpp passes.
// Only #include this file from voxel_chunk_pass.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <string>

// ── SkyboxPass / SunPass shared geometry ────────────────────────────────────

// Mirrors default_assets/shaders/skybox.vert + skybox.frag.
// The xyww trick (clip.z = clip.w) forces NDC depth = 1.0 (far plane) under
// both OpenGL's [-1,1] and WebGPU's [0,1] NDC z-range conventions.
static const char* SKYBOX_WGSL = R"(
struct Uniforms {
    viewProj: mat4x4<f32>,
    skyColor: vec4<f32>,
    horizonColor: vec4<f32>,
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) viewDir: vec3<f32>,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>) -> VSOut {
    var out: VSOut;
    out.viewDir = aPos;
    let pos = uniforms.viewProj * vec4<f32>(aPos, 1.0);
    out.position = vec4<f32>(pos.x, pos.y, pos.w, pos.w);
    return out;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    let dir = normalize(in.viewDir);
    let t = clamp((dir.y + 1.0) * 0.5, 0.0, 1.0);
    let col = mix(uniforms.horizonColor.rgb, uniforms.skyColor.rgb, t);
    return vec4<f32>(col, 1.0);
}
)";

// Mirrors default_assets/shaders/sun.vert + sun.frag. GL's fog hack
// (gl_FragCoord.z/gl_FragCoord.w) is replaced with an explicit world-space
// distance, computed from a cameraPos uniform — WGSL has no equivalent
// builtin reconstruction of pre-divide clip.w from gl_FragCoord.
static const char* SUN_WGSL = R"(
struct Uniforms {
    model: mat4x4<f32>,
    viewProj: mat4x4<f32>,
    color: vec4<f32>,
    fogColor: vec4<f32>,
    fogDensity: vec4<f32>,
    cameraPos: vec4<f32>,
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>) -> VSOut {
    var out: VSOut;
    let world = uniforms.model * vec4<f32>(aPos, 1.0);
    out.worldPos = world.xyz;
    out.position = uniforms.viewProj * world;
    return out;
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    let dist = length(in.worldPos - uniforms.cameraPos.xyz);
    let fogFactor = 1.0 - exp(-uniforms.fogDensity.x * dist * dist);
    let col = mix(uniforms.color.rgb, uniforms.fogColor.rgb, fogFactor);
    return vec4<f32>(col, 1.0);
}
)";

// Same cube geometry as Renderer's GL-only skyboxVAO (renderer.cpp); duplicated
// here since this pass owns its own WebGPU vertex buffer independent of the GL VAO.
static const float SKYBOX_CUBE_VERTS[] = {
    -1, -1, -1, 1,  -1, -1, 1,  1,  -1, 1,  1,  -1, -1, 1,  -1, -1, -1, -1, -1, -1, 1,  1,  -1, 1,  1,  1,  1,
    1,  1,  1,  -1, 1,  1,  -1, -1, 1,  -1, 1,  1,  -1, 1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  -1, 1,  1,
    1,  1,  1,  1,  1,  -1, 1,  -1, -1, 1,  -1, -1, 1,  -1, 1,  1,  1,  1,  -1, -1, -1, -1, -1, 1,  1,  -1, 1,
    1,  -1, 1,  1,  -1, -1, -1, -1, -1, -1, 1,  -1, -1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  -1, -1, 1,  -1,
};

// Same unit-quad geometry as SunPass::Execute's GL-only static sunVBO.
static const float SUN_QUAD_VERTS[] = {
    -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0,
};

// ── VoxelChunkPass ────────────────────────────────────────────────────────────
// WebGPU-spec-guaranteed-safe minUniformBufferOffsetAlignment.
constexpr uint32_t VOXEL_DRAW_SLOT_STRIDE = 256;
constexpr uint64_t VOXEL_FRAME_UNIFORM_SIZE = 176;// viewProj(64) + 7 * vec4(16)
constexpr uint64_t VOXEL_DRAW_UNIFORM_SIZE = 64;// model mat4x4

// Mirrors default_assets/shaders/voxel.vert + voxel.frag. v_uv from the GL
// vertex shader is dropped — voxel.frag never samples a texture, so it's
// dead data in both the GL and WGSL versions.
// FrameUniforms (binding 0) is written once per frame; DrawUniforms
// (binding 1) is a dynamic-offset binding with one 256-byte-strided slot
// per mesh, since wgpuQueueWriteBuffer ordering is relative to Queue::Submit
// and this engine batches the whole frame into one command buffer — writing
// a single shared uniform buffer per-draw inside a loop would make every
// draw observe only the last write.
static const char* VOXEL_WGSL = R"(
struct FrameUniforms {
    viewProj:    mat4x4<f32>,
    cameraPos:   vec4<f32>,
    lightDir:    vec4<f32>,
    lightColor:  vec4<f32>,
    ambient:     vec4<f32>,
    fogColor:    vec4<f32>,
    fogDensity:  vec4<f32>,
    // World-space clip plane (n, d) used by PlanarReflectionPass to cut away
    // geometry below the mirror plane; all-zero disables (dot == 0 is kept).
    clipPlane:   vec4<f32>,
};
struct DrawUniforms {
    model: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;

const FACE_NORMALS = array<vec3<f32>, 6>(
    vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, -1.0, 0.0),
    vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(-1.0, 0.0, 0.0),
    vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(0.0, 0.0, -1.0),
);

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) @interpolate(flat) voxelId: u32,
    @location(3) @interpolate(flat) faceId: u32,
};

@vertex
fn vs(@location(0) aPos: vec4<u32>, @location(1) aFace: vec4<u32>) -> VSOut {
    var out: VSOut;
    let localPos = vec3<f32>(aPos.xyz);
    let world = draw.model * vec4<f32>(localPos, 1.0);
    out.worldPos = world.xyz;
    let normalMat = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz);
    out.normal = normalMat * FACE_NORMALS[aFace.x];
    out.voxelId = aPos.w;
    out.faceId = aFace.x;
    out.position = frame.viewProj * world;
    return out;
}

fn palette(t: f32) -> vec3<f32> {
    let a = vec3<f32>(0.746, 0.815, 0.846);
    let b = vec3<f32>(0.195, 0.283, 0.187);
    let c = vec3<f32>(1.093, 1.417, 1.405);
    let d = vec3<f32>(5.435, 2.400, 5.741);
    return a + b * cos(6.28318 * (c * t + d));
}

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    if (dot(frame.clipPlane.xyz, in.worldPos) + frame.clipPlane.w < 0.0) {
        discard;
    }
    let norm = normalize(in.normal);
    let diff = max(dot(norm, normalize(frame.lightDir.xyz)), 0.0);
    var baseColor = palette(f32(in.voxelId) / 50.0);
    var faceShade = 0.9;
    if (in.faceId == 0u) { faceShade = 1.0; } else if (in.faceId == 1u) { faceShade = 0.5; }
    baseColor = baseColor * faceShade;
    let ambient = frame.ambient.rgb * baseColor;
    let diffuse = diff * frame.lightColor.rgb * baseColor;
    var color = ambient + diffuse;
    let dist = length(in.worldPos - frame.cameraPos.xyz);
    let fogFactor = clamp(exp(-frame.fogDensity.x * dist), 0.0, 1.0);
    color = mix(frame.fogColor.rgb, color, fogFactor);
    return vec4<f32>(color, 1.0);
}
)";

// ── WaterPass ─────────────────────────────────────────────────────────────────
constexpr uint32_t WATER_DRAW_SLOT_STRIDE = 256;
constexpr uint64_t WATER_FRAME_UNIFORM_SIZE =
    192;// viewProj(64) + reflViewProj(64) + cameraPos(16) + lightDir(16) + lightColor(16) + time(16)
constexpr uint64_t WATER_DRAW_UNIFORM_SIZE = 112;// model(64) + params0(16) + fogColor(16) + params1(16)

// Simplified port of default_assets/shaders/water.vert + water.frag: vertex
// wave displacement plus fresnel/diffuse/specular shading and distance fog.
// Drops the screen-space depth-texture Beer-Lambert thickness blend (no
// access to a bound depth texture in this pass) — a documented simplification.
// Planar reflection (group 1) mirrors the GL path: the world position is
// projected with PlanarReflectionPass's mirrored viewProj and the RT sampled
// there. params1.z = 0 (pass inactive or material opt-out) collapses the
// blend to the non-reflective shading, so the 1x1 black fallback texture
// bound in that case is never visible.
static const char* WATER_WGSL = R"(
struct FrameUniforms {
    viewProj:     mat4x4<f32>,
    reflViewProj: mat4x4<f32>,
    cameraPos:    vec4<f32>,
    lightDir:     vec4<f32>,
    lightColor:   vec4<f32>,
    time:         vec4<f32>,
};
struct DrawUniforms {
    model:    mat4x4<f32>,
    params0:  vec4<f32>, // waterLine, waveStrength, waveSpeed, fogDensity
    fogColor: vec4<f32>,
    params1:  vec4<f32>, // reflStrength, reflDistortion, reflEnabled, unused
};
@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(0) @binding(1) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var refl_tex: texture_2d<f32>;
@group(1) @binding(1) var refl_samp: sampler;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal:   vec3<f32>,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>, @location(1) aUV: vec2<f32>, @location(2) aNormal: vec3<f32>) -> VSOut {
    var out: VSOut;
    let waveStrength = draw.params0.y;
    let waveSpeed    = draw.params0.z;
    var pos = aPos;
    let wave = sin(frame.time.x * waveSpeed + aPos.x * 0.5)
             * cos(frame.time.x * waveSpeed + aPos.z * 0.5)
             * waveStrength;
    pos.y += wave;

    let world = draw.model * vec4<f32>(pos, 1.0);
    out.worldPos = world.xyz;
    let normalMat = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz);
    out.normal = normalize(normalMat * aNormal);
    out.position = frame.viewProj * world;
    return out;
}

const DEEP_COLOR:    vec3<f32> = vec3<f32>(0.04, 0.11, 0.35);
const SHALLOW_COLOR: vec3<f32> = vec3<f32>(0.686, 0.933, 0.933);

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    let waterLine  = draw.params0.x;
    let fogDensity = draw.params0.w;
    let fogColor   = draw.fogColor.rgb;

    let norm     = normalize(in.normal);
    let viewDir  = normalize(frame.cameraPos.xyz - in.worldPos);
    let lightDir = normalize(frame.lightDir.xyz);
    let halfDir  = normalize(lightDir + viewDir);

    if (frame.cameraPos.y < waterLine) {
        let sub = smoothstep(2.0, 32.0, waterLine - frame.cameraPos.y);
        return vec4<f32>(mix(SHALLOW_COLOR, DEEP_COLOR, sub), 0.9);
    }

    let fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 5.0);
    var col = mix(SHALLOW_COLOR, DEEP_COLOR, fresnel);

    let diff = max(dot(norm, lightDir), 0.0);
    let spec = pow(max(dot(norm, halfDir), 0.0), 128.0);
    col = mix(col, col * diff * frame.lightColor.rgb + vec3<f32>(1.0) * spec * frame.lightColor.rgb, 0.2);

    // Planar reflection: project this surface point with the mirrored
    // camera's viewProj. WebGPU NDC y is up while texture v is down, so the
    // y component flips (the GL shader samples without the flip).
    let reflStrength   = draw.params1.x * draw.params1.z;
    let reflDistortion = draw.params1.y;
    var reflAmount     = 0.0;
    if (reflStrength > 0.0) {
        let reflClip = frame.reflViewProj * vec4<f32>(in.worldPos, 1.0);
        let reflNdc  = reflClip.xy / reflClip.w;
        var reflUV   = vec2<f32>(reflNdc.x, -reflNdc.y) * 0.5 + 0.5;
        reflUV      += norm.xz * reflDistortion;
        let reflCol  = textureSample(refl_tex, refl_samp,
                                     clamp(reflUV, vec2<f32>(0.001), vec2<f32>(0.999))).rgb;
        reflAmount   = clamp(reflStrength * (0.15 + 0.85 * fresnel), 0.0, 1.0);
        col = mix(col, reflCol, reflAmount);
    }
    col = mix(col, vec3<f32>(1.0), fresnel * 0.5 * (1.0 - reflStrength));

    let dist = length(in.worldPos - frame.cameraPos.xyz);
    col = mix(fogColor, col, clamp(exp(-fogDensity * dist * dist), 0.0, 1.0));

    // Mirrored sky/terrain shouldn't fade out through the water's own
    // transparency — raise opacity where the reflection dominates.
    return vec4<f32>(col, max(smoothstep(0.1, 0.9, fresnel + 0.3), reflAmount));
}
)";

// ── BloomPass ─────────────────────────────────────────────────────────────────
// Shared fullscreen-triangle vertex stage (see PostProcessPass's POSTPROCESS_WGSL).
static const char* BLOOM_VS_WGSL = R"(
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
)";

// Hard luminance cutoff — an exact port of GL's bloom_threshold.frag: pixels
// at/below the threshold are dropped, brighter ones pass through unmodified.
static const std::string BLOOM_THRESH_WGSL = std::string(BLOOM_VS_WGSL) + R"(
struct ThreshUniforms { threshold: f32, _pad0: f32, _pad1: f32, _pad2: f32 };
@group(0) @binding(0) var<uniform> u: ThreshUniforms;
@group(0) @binding(1) var srcTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    let c = textureSample(srcTex, samp, in.uv).rgb;
    let brightness = dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
    if (brightness > u.threshold) {
        return vec4<f32>(c, 1.0);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}
)";

// 13-tap Kawase-style downsample — port of GL's bloom_downsample.frag. Halves
// resolution while pre-filtering, one level of the mip pyramid per invocation.
static const std::string BLOOM_DOWN_WGSL = std::string(BLOOM_VS_WGSL) + R"(
struct DownUniforms { texelSize: vec2<f32>, _pad: vec2<f32> };
@group(0) @binding(0) var<uniform> u: DownUniforms;
@group(0) @binding(1) var srcTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    let t = u.texelSize;
    let uv = in.uv;
    let a = textureSample(srcTex, samp, uv + t * vec2<f32>(-1.0, -1.0)).rgb;
    let b = textureSample(srcTex, samp, uv + t * vec2<f32>( 0.0, -1.0)).rgb;
    let c = textureSample(srcTex, samp, uv + t * vec2<f32>( 1.0, -1.0)).rgb;
    let d = textureSample(srcTex, samp, uv + t * vec2<f32>(-0.5, -0.5)).rgb;
    let e = textureSample(srcTex, samp, uv + t * vec2<f32>( 0.5, -0.5)).rgb;
    let f = textureSample(srcTex, samp, uv + t * vec2<f32>(-1.0,  0.0)).rgb;
    let g = textureSample(srcTex, samp, uv).rgb;
    let h = textureSample(srcTex, samp, uv + t * vec2<f32>( 1.0,  0.0)).rgb;
    let i = textureSample(srcTex, samp, uv + t * vec2<f32>(-0.5,  0.5)).rgb;
    let j = textureSample(srcTex, samp, uv + t * vec2<f32>( 0.5,  0.5)).rgb;
    let k = textureSample(srcTex, samp, uv + t * vec2<f32>(-1.0,  1.0)).rgb;
    let l = textureSample(srcTex, samp, uv + t * vec2<f32>( 0.0,  1.0)).rgb;
    let m = textureSample(srcTex, samp, uv + t * vec2<f32>( 1.0,  1.0)).rgb;
    var result = g * 0.125;
    result += (b + f + h + l) * 0.0625;
    result += (a + c + k + m) * 0.03125;
    result += (d + e + i + j) * 0.125;
    return vec4<f32>(result, 1.0);
}
)";

// 9-tap tent upsample — port of GL's bloom_upsample.frag. Drawn with additive
// blending so each level accumulates onto the coarser one already in the mip.
static const std::string BLOOM_UP_WGSL = std::string(BLOOM_VS_WGSL) + R"(
struct UpUniforms { texelSize: vec2<f32>, filterRadius: f32, _pad: f32 };
@group(0) @binding(0) var<uniform> u: UpUniforms;
@group(0) @binding(1) var srcTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    let r = u.filterRadius;
    let t = u.texelSize;
    let uv = in.uv;
    let a = textureSample(srcTex, samp, uv + t * vec2<f32>(-r,  r)).rgb;
    let b = textureSample(srcTex, samp, uv + t * vec2<f32>(0.0,  r)).rgb;
    let c = textureSample(srcTex, samp, uv + t * vec2<f32>( r,  r)).rgb;
    let d = textureSample(srcTex, samp, uv + t * vec2<f32>(-r, 0.0)).rgb;
    let e = textureSample(srcTex, samp, uv).rgb;
    let f = textureSample(srcTex, samp, uv + t * vec2<f32>( r, 0.0)).rgb;
    let g = textureSample(srcTex, samp, uv + t * vec2<f32>(-r, -r)).rgb;
    let h = textureSample(srcTex, samp, uv + t * vec2<f32>(0.0, -r)).rgb;
    let i = textureSample(srcTex, samp, uv + t * vec2<f32>( r, -r)).rgb;
    var result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i) * 1.0;
    result = result / 16.0;
    return vec4<f32>(result, 1.0);
}
)";

// Additive composite: original scene (snapshotted before this pass, since
// sceneRT cannot be bound as both a render attachment and a sampled texture
// in the same pass) plus the blurred bright-pass result.
static const std::string BLOOM_COMP_WGSL = std::string(BLOOM_VS_WGSL) + R"(
struct CompUniforms { bloomStrength: f32, _pad0: f32, _pad1: f32, _pad2: f32 };
@group(0) @binding(0) var<uniform> u: CompUniforms;
@group(0) @binding(1) var sceneTex: texture_2d<f32>;
@group(0) @binding(2) var bloomTex: texture_2d<f32>;
@group(0) @binding(3) var samp: sampler;

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
    let scene = textureSample(sceneTex, samp, in.uv).rgb;
    let bloom = textureSample(bloomTex, samp, in.uv).rgb;
    return vec4<f32>(scene + bloom * u.bloomStrength, 1.0);
}
)";

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

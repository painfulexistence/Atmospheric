#pragma once
// Internal WGSL shader sources for portal_pass.cpp.
// Only #include this file from portal_pass.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ── Portal surface ────────────────────────────────────────────────────────────
// WebGPU-spec-guaranteed-safe minUniformBufferOffsetAlignment.
constexpr uint32_t PORTAL_SURF_SLOT_STRIDE = 256;
// model(64) + viewProj(64) + portalViewProj(64) + params0(16) + cameraPos(16)
constexpr uint64_t PORTAL_SURF_UNIFORM_SIZE = 224;

// Mirrors default_assets/shaders/portal.vert + portal.frag. Everything lives
// in one dynamic-offset slot per surface draw: DrawPortalSurfaces runs many
// times per frame (main view + every recursion level) and wgpuQueueWriteBuffer
// ordering is relative to Queue::Submit, so each draw must own its slot.
// params0 = (rimColor.rgb, hasView). Reflection-style y-flip when sampling:
// WebGPU NDC y is up while texture v is down.
static const char* PORTAL_SURFACE_WGSL = R"(
struct DrawUniforms {
    model:          mat4x4<f32>,
    viewProj:       mat4x4<f32>,
    portalViewProj: mat4x4<f32>,
    params0:        vec4<f32>,
    cameraPos:      vec4<f32>,
};
@group(0) @binding(0) var<uniform> draw: DrawUniforms;
@group(1) @binding(0) var portal_tex: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) uv:       vec2<f32>,
    @location(2) normal:   vec3<f32>,
};

@vertex
fn vs(@location(0) aPos: vec3<f32>, @location(1) aUV: vec2<f32>, @location(2) aNormal: vec3<f32>) -> VSOut {
    var out: VSOut;
    let world = draw.model * vec4<f32>(aPos, 1.0);
    out.worldPos = world.xyz;
    let normalMat = mat3x3<f32>(draw.model[0].xyz, draw.model[1].xyz, draw.model[2].xyz);
    out.normal = normalize(normalMat * aNormal);
    out.uv = aUV;
    out.position = draw.viewProj * world;
    return out;
}

const VOID_COLOR: vec3<f32> = vec3<f32>(0.01, 0.01, 0.03);

@fragment
fn fs(in: VSOut) -> @location(0) vec4<f32> {
    // Concentric disc UVs: rim distance is 0 at the center, 1 at the edge.
    let r = length(in.uv - vec2<f32>(0.5)) * 2.0;

    // Only the front (+Z) side is a window; the back shows the void.
    let facing = dot(normalize(in.normal), normalize(draw.cameraPos.xyz - in.worldPos));

    // Sample unconditionally (uniform control flow for textureSample), then
    // select — `facing` is a varying, so branching before the sample would be
    // non-uniform and rejected by WGSL validation.
    let clip = draw.portalViewProj * vec4<f32>(in.worldPos, 1.0);
    let ndc  = clip.xy / max(abs(clip.w), 0.0001) * sign(clip.w);
    let uv   = clamp(vec2<f32>(ndc.x, -ndc.y) * 0.5 + 0.5, vec2<f32>(0.001), vec2<f32>(0.999));
    let sampled = textureSample(portal_tex, samp, uv).rgb;

    var col = VOID_COLOR;
    if (draw.params0.w > 0.5 && facing > 0.0) {
        col = sampled;
    }

    // HDR rim glow (feeds bloom), matching the game's colored portal edges.
    let rim = smoothstep(0.82, 0.95, r);
    col = mix(col, draw.params0.xyz * 2.5, rim);

    return vec4<f32>(col, 1.0);
}
)";

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

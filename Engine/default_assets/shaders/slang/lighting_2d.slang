// lighting_2d.slang — 2D point-light fullscreen post-process (reference port)
//
// Single Slang source that replaces the hand-maintained trio:
//   lighting_2d.vert + lighting_2d.frag (GL 4.1 / GLSL ES 300)
//   lighting_2d.wgsl                    (WebGPU)
//
// Build it with `scripts/compile_slang.py` or -DAE_USE_SLANG=ON; see
// docs/slang-migration.md for the pipeline and the GL-side UBO requirement.
//
// The uniform block below is laid out to match the existing lighting_2d.wgsl
// (WebGPU std140): ambient vec4, lightCount + 3 pad words, then 16 lights of
// 48 bytes each. This is the canonical layout both backends now share.
//
// @slang-entry vs:vertex
// @slang-entry fs:fragment

static const int MAX_LIGHTS = 16;

struct Light {
    float2 pos;
    float2 _pad;
    float4 color;
    float  radius;
    float  intensity;
    float2 _pad2;
};

struct LightingUniforms {
    float4 ambient;
    uint   lightCount;
    uint   _p0;
    uint   _p1;
    uint   _p2;
    Light  lights[MAX_LIGHTS];
};

[[vk::binding(0, 0)]] ConstantBuffer<LightingUniforms> uni;
[[vk::binding(1, 0)]] Texture2D    sceneTex;
[[vk::binding(2, 0)]] SamplerState samp;

struct VOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// Fullscreen triangle from the vertex id — no vertex buffer bound.
[shader("vertex")]
VOut vs(uint vi : SV_VertexID) {
    float x = float((vi << 1) & 2) * 2.0 - 1.0;
    float y = float(vi & 2) * -2.0 + 1.0;

    VOut o;
    o.pos = float4(x, y, 0.0, 1.0);
    o.uv  = float2(x * 0.5 + 0.5, 0.5 - y * 0.5);
    return o;
}

[shader("fragment")]
float4 fs(VOut inp) : SV_Target {
    float4 scene = sceneTex.Sample(samp, inp.uv);

    uint w, h;
    sceneTex.GetDimensions(w, h);
    float2 pixPos = inp.uv * float2(float(w), float(h));

    float3 lightAccum = uni.ambient.rgb * uni.ambient.a;

    for (uint i = 0u; i < uni.lightCount && i < uint(MAX_LIGHTS); i++) {
        Light l = uni.lights[i];
        float d     = length(pixPos - l.pos);
        float atten = max(0.0, 1.0 - d / l.radius);
        lightAccum += l.color.rgb * (l.intensity * atten * atten);
    }

    return float4(scene.rgb * min(lightAccum, float3(1.0, 1.0, 1.0)), scene.a);
}

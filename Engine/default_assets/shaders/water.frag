#version 410 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform vec3      u_lightDir;
uniform vec3      u_lightColor;
uniform vec3      u_cameraPos;
uniform vec3      u_fogColor;
uniform float     u_fogDensity;
uniform float     u_waterLine;
uniform sampler2D u_depthTexture;
uniform mat4      u_invProj;
uniform mat4      u_invView;
uniform vec2      u_screenSize;
uniform vec3      u_deepColor;
uniform vec3      u_shallowColor;
uniform float     u_beerCoef;

// 0 in auxiliary views (portals) where no per-view scene depth is available:
// the Beer-Lambert thickness blend is skipped in favour of a fresnel-based
// deep/shallow mix, matching the WebGPU water's documented simplification.
uniform float     u_useDepth;

// Planar reflection, rendered by PlanarReflectionPass. u_reflStrength is 0
// when the pass is inactive or the material opted out, which fully disables
// the blend (the sampler unit then holds no meaningful texture).
uniform sampler2D u_reflectionTex;
uniform mat4      u_reflViewProj;
uniform float     u_reflStrength;
uniform float     u_reflDistortion;

out vec4 fragColor;

vec3 reconstructWorldPos(vec2 screenUV, float depth) {
    vec4 clipPos = vec4(screenUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = u_invProj * clipPos;
    viewPos     /= viewPos.w;
    return (u_invView * viewPos).xyz;
}

void main() {
    vec3 norm     = normalize(v_normal);
    vec3 viewDir  = normalize(u_cameraPos - v_worldPos);
    vec3 lightDir = normalize(u_lightDir);
    vec3 halfDir  = normalize(lightDir + viewDir);

    // Underwater camera tint
    if (u_cameraPos.y < u_waterLine) {
        float sub = smoothstep(2.0, 32.0, u_waterLine - u_cameraPos.y);
        fragColor = vec4(mix(u_shallowColor, u_deepColor, sub), 0.9);
        return;
    }

    float diff    = max(dot(norm, lightDir), 0.0);
    float spec    = pow(max(dot(norm, halfDir), 0.0), 128.0);
    float fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 5.0);

    // Beer-Lambert depth via screen-space depth reconstruction. Auxiliary views
    // (portals) have no matching scene-depth texture, so fall back to a
    // fresnel-driven deep/shallow blend there (u_useDepth == 0).
    float beerFactor;
    if (u_useDepth > 0.5) {
        vec2  screenUV       = gl_FragCoord.xy / u_screenSize;
        float rawDepth       = texture(u_depthTexture, screenUV).r;
        vec3  floorPos       = reconstructWorldPos(screenUV, rawDepth);
        float waterThickness = max(v_worldPos.y - floorPos.y, 0.0);
        beerFactor           = max(1.0 - exp(-waterThickness * u_beerCoef), 0.0);
    } else {
        beerFactor = fresnel;
    }
    vec3 col = mix(u_shallowColor, u_deepColor, beerFactor);

    col = mix(col, col * diff * u_lightColor + vec3(1.0) * spec * u_lightColor, 0.2);

    // Planar reflection: project this surface point with the mirrored
    // camera's viewProj and sample the reflection RT there, wobbled by the
    // wave normal. Strongest at grazing angles (fresnel), like the old
    // white highlight it replaces.
    float reflAmount = 0.0;
    if (u_reflStrength > 0.0) {
        vec4 reflClip = u_reflViewProj * vec4(v_worldPos, 1.0);
        vec2 reflUV   = reflClip.xy / reflClip.w * 0.5 + 0.5;
        reflUV       += norm.xz * u_reflDistortion;
        vec3 reflCol  = texture(u_reflectionTex, clamp(reflUV, vec2(0.001), vec2(0.999))).rgb;
        reflAmount    = clamp(u_reflStrength * (0.15 + 0.85 * fresnel), 0.0, 1.0);
        col = mix(col, reflCol, reflAmount);
    }
    col = mix(col, vec3(1.0), fresnel * 0.5 * (1.0 - u_reflStrength));

    float dist = length(v_worldPos - u_cameraPos);
    col = mix(u_fogColor, col, clamp(exp(-u_fogDensity * dist * dist), 0.0, 1.0));

    // Mirrored sky/terrain shouldn't fade out through the water's own
    // transparency — raise opacity where the reflection dominates.
    fragColor = vec4(col, max(smoothstep(0.1, 0.9, beerFactor + 0.3), reflAmount));
}

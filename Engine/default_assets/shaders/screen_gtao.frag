#version 410 core

// Screen-space horizon-based ambient occlusion (GTAO/HBAO family) over the
// resolved opaque scene. View-space position and normal are reconstructed from
// the depth buffer — voxel faces are planar so depth-derived normals are clean,
// and this works for any geometry with no G-buffer. Occlusion is integrated as
// the max elevation above the tangent plane over a few screen-space directions.
// Output = scene color * AO; ScreenSpaceGIPass composites it back.

in vec2 v_uv;

uniform sampler2D u_color;
uniform sampler2D u_depth;
uniform mat4  u_proj;
uniform mat4  u_invProj;
uniform vec2  u_viewportSize;
uniform float u_aoRadius;  // view-space radius in metres
uniform float u_aoStrength;// 0..1

out vec4 fragColor;

const int DIRS = 8;
const int STEPS = 4;
const float TWO_PI = 6.2831853;

vec3 viewPos(vec2 uv, float d) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_invProj * ndc;
    return v.xyz / v.w;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float d = texture(u_depth, v_uv).r;
    vec3 col = texture(u_color, v_uv).rgb;
    if (d >= 1.0) {
        fragColor = vec4(col, 1.0);// sky / no geometry: no AO
        return;
    }

    vec3 P = viewPos(v_uv, d);
    // Normal from the screen-space derivatives of view position (clean on the
    // world's planar voxel faces). Flip toward the camera (view dir = -P).
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, -P) < 0.0) N = -N;

    // Project the world-space radius to a screen pixel radius at this depth.
    float radiusPx = u_aoRadius * u_proj[1][1] * 0.5 * u_viewportSize.y / max(0.001, -P.z);
    radiusPx = min(radiusPx, 0.15 * u_viewportSize.y);// clamp huge near-field footprints

    float rot = hash(gl_FragCoord.xy) * TWO_PI;// per-pixel rotation trades banding for noise
    float occ = 0.0;
    for (int i = 0; i < DIRS; ++i) {
        float a = rot + float(i) * (TWO_PI / float(DIRS));
        vec2 dir = vec2(cos(a), sin(a));
        float horizon = 0.0;// max sin(elevation) above the tangent plane
        for (int s = 1; s <= STEPS; ++s) {
            vec2 suv = v_uv + dir * (radiusPx * (float(s) / float(STEPS))) / u_viewportSize;
            if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) break;
            float sd = texture(u_depth, suv).r;
            if (sd >= 1.0) continue;
            vec3 H = viewPos(suv, sd) - P;
            float dist = length(H);
            if (dist < 1e-4) continue;
            float falloff = clamp(1.0 - dist / u_aoRadius, 0.0, 1.0);
            horizon = max(horizon, dot(H / dist, N) * falloff);
        }
        occ += horizon;
    }
    occ /= float(DIRS);

    float ao = mix(1.0, clamp(1.0 - occ, 0.0, 1.0), u_aoStrength);
    fragColor = vec4(col * ao, 1.0);
}

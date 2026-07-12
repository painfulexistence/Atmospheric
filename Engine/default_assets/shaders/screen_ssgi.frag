#version 410 core

// Screen-space global illumination — one-bounce diffuse gather. Reconstructs
// view-space position + normal from depth (as GTAO does), casts a few cosine-
// weighted hemisphere rays, marches each against the depth buffer, and on a hit
// samples the resolved scene color there as incoming bounce radiance. Output is
// the averaged indirect radiance (noisy — the composite pass blurs it before
// adding it to the scene). No G-buffer; works for any geometry.

in vec2 v_uv;

uniform sampler2D u_color;
uniform sampler2D u_depth;
uniform mat4  u_proj;
uniform mat4  u_invProj;
uniform vec2  u_viewportSize;
uniform float u_ssgiRadius;   // view-space march distance (metres)
uniform float u_ssgiThickness;// max depth gap counted as a hit (metres)
uniform int   u_frameIndex;   // decorrelates the per-pixel sample rotation

out vec4 fragColor;

const int RAYS = 4;
const int STEPS = 8;
const float PI = 3.1415927;

vec3 viewPos(vec2 uv, float d) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_invProj * ndc;
    return v.xyz / v.w;
}

vec2 hash2(vec2 p, int k) {
    p += float(k) * 0.61803399;
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

vec3 cosineDir(vec3 n, vec2 xi) {
    float phi = 2.0 * PI * xi.x;
    float ct = sqrt(1.0 - xi.y);
    float st = sqrt(xi.y);
    vec3 t = normalize(cross(abs(n.x) > 0.5 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n));
    vec3 b = cross(n, t);
    return normalize(t * cos(phi) * st + b * sin(phi) * st + n * ct);
}

void main() {
    float d = texture(u_depth, v_uv).r;
    if (d >= 1.0) {
        fragColor = vec4(0.0);// sky: no bounce
        return;
    }
    vec3 P = viewPos(v_uv, d);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, -P) < 0.0) N = -N;

    vec3 indirect = vec3(0.0);
    float stepLen = u_ssgiRadius / float(STEPS);
    for (int i = 0; i < RAYS; ++i) {
        vec2 xi = hash2(gl_FragCoord.xy, i + u_frameIndex * RAYS);
        vec3 dir = cosineDir(N, xi);
        vec3 rp = P + N * 0.02;// bias off the surface
        for (int s = 0; s < STEPS; ++s) {
            rp += dir * stepLen;
            vec4 clip = u_proj * vec4(rp, 1.0);
            if (clip.w <= 0.0) break;
            vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
            if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) break;
            float sd = texture(u_depth, suv).r;
            if (sd >= 1.0) continue;
            vec3 surf = viewPos(suv, sd);
            // Hit: the ray has passed behind a nearer surface (surf closer to
            // the camera than the ray point) by less than the thickness.
            float gap = surf.z - rp.z;
            if (gap > 0.0 && gap < u_ssgiThickness) {
                indirect += texture(u_color, suv).rgb;
                break;
            }
        }
    }
    fragColor = vec4(indirect / float(RAYS), 1.0);// misses contribute 0
}

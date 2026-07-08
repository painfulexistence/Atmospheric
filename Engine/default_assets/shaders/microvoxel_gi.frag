#version 410 core

// ============================================================================
// Micro Voxel GI trace + temporal accumulation — GL 4.1 / WebGL2 path
// ============================================================================
// One diffuse bounce per pixel per frame, accumulated over time:
//   primary DDA -> cosine-weighted bounce ray -> radiance from the bounce hit
//   (its albedo lit by the sun, with a shadow ray) or from the sky on miss.
// The result is INCIDENT radiance at the primary hit (albedo of the primary
// surface is NOT applied here — the composite pass modulates by albedo, so
// accumulation blurs lighting, not texture detail).
// Temporal: the primary hit's exact world position is reprojected with the
// previous frame's viewProj (the volume is static, so this is exact for
// camera motion); history is validated by stored camera distance (alpha).
//
// DDA / volume bindings duplicate microvoxel.frag — keep them in sync.

#ifdef GL_ES
precision highp float;
precision highp int;
precision highp usampler3D;
precision highp sampler2D;
#endif

in vec2 v_uv;

uniform usampler3D u_volume;
uniform usampler3D u_occupancy;
uniform sampler2D  u_palette;
uniform sampler2D  u_history;      // rgb = accumulated indirect, a = camera distance

uniform mat4  u_invViewProj;
uniform mat4  u_prevViewProj;
uniform vec3  u_cameraPos;
uniform vec3  u_prevCameraPos;
uniform vec3  u_volumeOrigin;
uniform float u_voxelSize;
uniform int   u_gridDim;
uniform int   u_brickDim;
uniform int   u_maxRaySteps;
uniform vec3  u_sunDir;
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform int   u_frameIndex;
uniform float u_blend;             // history weight (e.g. 0.93)
uniform float u_emissiveStrength;  // HDR multiplier for palette-alpha emission

out vec4 fragColor;

const float PI = 3.1415927;

struct Hit {
    float t;
    vec3  normal;
    uint  material;
    bool  hit;
};

// ── DDA (keep in sync with microvoxel.frag) ─────────────────────────────────

Hit traverseBrick(vec3 ro, vec3 rd, vec3 invDir, float tStart, ivec3 brickCell, vec3 enterNormal) {
    Hit r;
    r.hit = false; r.t = tStart; r.normal = enterNormal; r.material = 0u;

    int bd = u_brickDim;
    ivec3 lo = brickCell * bd;
    ivec3 hi = lo + bd - 1;

    float eps = u_voxelSize * 1e-3;
    vec3 p = ro + rd * (tStart + eps);
    ivec3 cell = clamp(ivec3(floor((p - u_volumeOrigin) / u_voxelSize)), lo, hi);

    ivec3 stepDir = ivec3(sign(rd));
    vec3 tDelta = abs(vec3(u_voxelSize) * invDir);
    vec3 stepPos = vec3(greaterThan(rd, vec3(0.0)));
    vec3 boundary = u_volumeOrigin + (vec3(cell) + stepPos) * u_voxelSize;
    vec3 tMax = mix(vec3(1e30), (boundary - ro) * invDir, notEqual(rd, vec3(0.0)));

    float t = tStart;
    vec3 normal = enterNormal;

    for (int i = 0; i < 3 * bd + 1; i++) {
        uint mat = texelFetch(u_volume, cell, 0).r;
        if (mat != 0u) {
            r.hit = true; r.t = t; r.normal = normal; r.material = mat;
            return r;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x += tDelta.x; cell.x += stepDir.x;
            normal = vec3(-float(stepDir.x), 0.0, 0.0);
            if (cell.x < lo.x || cell.x > hi.x) break;
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y += tDelta.y; cell.y += stepDir.y;
            normal = vec3(0.0, -float(stepDir.y), 0.0);
            if (cell.y < lo.y || cell.y > hi.y) break;
        } else {
            t = tMax.z; tMax.z += tDelta.z; cell.z += stepDir.z;
            normal = vec3(0.0, 0.0, -float(stepDir.z));
            if (cell.z < lo.z || cell.z > hi.z) break;
        }
    }
    return r;
}

Hit raycast(vec3 ro, vec3 rd) {
    Hit r;
    r.hit = false; r.t = 0.0; r.normal = vec3(0.0); r.material = 0u;

    vec3 invDir = mix(vec3(1e30), 1.0 / rd, notEqual(rd, vec3(0.0)));
    vec3 bmin = u_volumeOrigin;
    vec3 bmax = bmin + vec3(float(u_gridDim)) * u_voxelSize;

    vec3 tA = (bmin - ro) * invDir;
    vec3 tB = (bmax - ro) * invDir;
    vec3 tNear = min(tA, tB);
    vec3 tFar  = max(tA, tB);
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit  = min(min(tFar.x, tFar.y), tFar.z);
    if (tExit <= max(tEnter, 0.0)) return r;

    vec3 enterNormal;
    if (tEnter > 0.0) {
        if (tNear.x > tNear.y && tNear.x > tNear.z)      enterNormal = vec3(-sign(rd.x), 0.0, 0.0);
        else if (tNear.y > tNear.z)                      enterNormal = vec3(0.0, -sign(rd.y), 0.0);
        else                                             enterNormal = vec3(0.0, 0.0, -sign(rd.z));
    } else {
        vec3 a = abs(rd);
        if (a.x > a.y && a.x > a.z)      enterNormal = vec3(-sign(rd.x), 0.0, 0.0);
        else if (a.y > a.z)              enterNormal = vec3(0.0, -sign(rd.y), 0.0);
        else                             enterNormal = vec3(0.0, 0.0, -sign(rd.z));
    }

    float brickSize = u_voxelSize * float(u_brickDim);
    ivec3 brickGrid = ivec3(u_gridDim / u_brickDim);

    float t = max(tEnter, 0.0);
    float eps = u_voxelSize * 1e-3;
    vec3 p = ro + rd * (t + eps);
    ivec3 cell = clamp(ivec3(floor((p - bmin) / brickSize)), ivec3(0), brickGrid - 1);

    ivec3 stepDir = ivec3(sign(rd));
    vec3 tDelta = abs(vec3(brickSize) * invDir);
    vec3 stepPos = vec3(greaterThan(rd, vec3(0.0)));
    vec3 boundary = bmin + (vec3(cell) + stepPos) * brickSize;
    vec3 tMax = mix(vec3(1e30), (boundary - ro) * invDir, notEqual(rd, vec3(0.0)));

    vec3 normal = enterNormal;

    for (int i = 0; i < u_maxRaySteps; i++) {
        if (texelFetch(u_occupancy, cell, 0).r != 0u) {
            Hit h = traverseBrick(ro, rd, invDir, t, cell, normal);
            if (h.hit) return h;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x += tDelta.x; cell.x += stepDir.x;
            normal = vec3(-float(stepDir.x), 0.0, 0.0);
            if (cell.x < 0 || cell.x >= brickGrid.x) break;
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y += tDelta.y; cell.y += stepDir.y;
            normal = vec3(0.0, -float(stepDir.y), 0.0);
            if (cell.y < 0 || cell.y >= brickGrid.y) break;
        } else {
            t = tMax.z; tMax.z += tDelta.z; cell.z += stepDir.z;
            normal = vec3(0.0, 0.0, -float(stepDir.z));
            if (cell.z < 0 || cell.z >= brickGrid.z) break;
        }
        if (t > tExit) break;
    }
    return r;
}

// ── GI ──────────────────────────────────────────────────────────────────────

// Per-pixel, per-frame decorrelated random pair
vec2 rand2(ivec2 pix, int frame) {
    uint h = uint(pix.x) * 374761393u + uint(pix.y) * 668265263u + uint(frame) * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    uint h2 = (h ^ (h >> 16u)) * 2654435761u;
    return vec2(float(h & 0xFFFFu), float(h2 & 0xFFFFu)) / 65535.0;
}

vec3 skyRadiance(vec3 dir) {
    return mix(vec3(0.20, 0.22, 0.28), vec3(0.45, 0.55, 0.75), dir.y * 0.5 + 0.5);
}

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 farH = u_invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = u_cameraPos;
    vec3 rd = normalize(farH.xyz / farH.w - ro);

    Hit h = raycast(ro, rd);
    if (!h.hit) {
        fragColor = vec4(0.0);// alpha 0 = no history
        return;
    }

    vec3 hitPos = ro + rd * h.t;

    // Cosine-weighted hemisphere sample around the (axis-aligned) hit normal.
    // With cosine sampling the estimator is just the incoming radiance.
    vec3 n = h.normal;
    vec3 t1 = (abs(n.x) > 0.5) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = normalize(cross(n, t1));
    t1 = cross(t2, n);
    vec2 xi = rand2(ivec2(gl_FragCoord.xy), u_frameIndex);
    float phi = 2.0 * PI * xi.x;
    float sq = sqrt(xi.y);
    vec3 bounceDir = normalize(t1 * cos(phi) * sq + t2 * sin(phi) * sq + n * sqrt(1.0 - xi.y));

    vec3 bounceOrigin = hitPos + n * u_voxelSize * 0.51;
    Hit b = raycast(bounceOrigin, bounceDir);

    vec3 radiance;
    if (!b.hit) {
        radiance = skyRadiance(bounceDir);// sky light, occluded naturally
    } else {
        vec3 bPos = bounceOrigin + bounceDir * b.t;
        vec4 bPal = texelFetch(u_palette, ivec2(int(b.material), 0), 0);
        vec3 bAlbedo = bPal.rgb;
        float bEmission = bPal.a;    // emissive voxels act as area lights here
        vec3 L = normalize(u_sunDir);
        float bNdl = max(dot(b.normal, L), 0.0);
        float bShadow = 1.0;
        if (bNdl > 0.0) {
            Hit sh = raycast(bPos + b.normal * u_voxelSize * 0.51, L);
            if (sh.hit) bShadow = 0.0;
        }
        // One-bounce: the bounce surface's direct sun light plus a small sky
        // term standing in for further bounces, plus any self-emission (this is
        // what makes glowing voxels bleed warm light onto their neighbors).
        radiance = bAlbedo * (u_sunColor * u_sunIntensity * (1.0 / PI) * bNdl * bShadow
                              + skyRadiance(b.normal) * 0.3)
                   + bAlbedo * bEmission * u_emissiveStrength;
    }

    // Temporal accumulation: reproject the exact hit position into last frame.
    float camDist = length(hitPos - u_cameraPos);
    vec3 result = radiance;
    vec4 prevClip = u_prevViewProj * vec4(hitPos, 1.0);
    if (prevClip.w > 0.0) {
        vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
        if (all(greaterThanEqual(prevUV, vec2(0.0))) && all(lessThanEqual(prevUV, vec2(1.0)))) {
            vec4 hist = texture(u_history, prevUV);
            // Validate: the stored sample must have seen (nearly) the same
            // point — compare its camera distance from the previous eye.
            float expected = length(hitPos - u_prevCameraPos);
            if (hist.a > 0.0 && abs(hist.a - expected) < 0.05 * expected + u_voxelSize) {
                result = mix(radiance, hist.rgb, u_blend);
            }
        }
    }

    fragColor = vec4(result, camDist);
}

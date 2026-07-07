#version 410 core

// ============================================================================
// Micro Voxel Raymarch (experimental) — GL 4.1 / WebGL2 path
// ============================================================================
// Two-level DDA over a dense voxel volume stored in a 3D texture:
//   - coarse pass strides one brick (BRICK_DIM voxels) at a time, skipping
//     empty bricks in a single step via the coarse occupancy texture;
//   - fine pass walks individual voxels inside occupied bricks.
// Hits write gl_FragDepth so voxels depth-composite with the rasterized scene
// (this pass runs after ForwardOpaque, before the sky fills remaining pixels).
//
// Storage (all CPU-built, uploaded once; edits re-upload affected regions):
//   u_volume    usampler3D  R8UI, gridDim^3, per-voxel palette index (0 = air)
//   u_occupancy usampler3D  R8UI, (gridDim/BRICK_DIM)^3, nonzero = brick solid
//   u_palette   sampler2D   256x1 RGBA8, albedo per material index

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
uniform sampler2D  u_giTex;    // accumulated 1-bounce indirect (microvoxel_gi.frag)

uniform mat4  u_invViewProj;   // clip -> world
uniform mat4  u_viewProj;      // world -> clip (for depth output)
uniform vec3  u_cameraPos;
uniform vec3  u_volumeOrigin;  // world-space min corner
uniform float u_voxelSize;     // world edge length of one voxel
uniform int   u_gridDim;       // voxels per volume edge (cubic)
uniform int   u_brickDim;      // voxels per brick edge (8)
uniform int   u_maxRaySteps;   // cap on coarse DDA iterations
uniform vec3  u_sunDir;        // normalized, toward the sun
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform float u_ambient;
uniform int   u_shadowEnabled;
uniform float u_aoStrength;    // 0 disables corner AO
uniform float u_giStrength;    // 0 = flat ambient, >0 = traced indirect
uniform int   u_debugMode;     // 0=off 1=albedo 2=normal 3=ao 4=shadow 5=gi 6=material
uniform float u_emissiveStrength;  // HDR multiplier for palette-alpha emission
uniform int   u_reflectionsEnabled;// per-material mirror reflections (row 1 of palette)

// Local point lights (warm fill). Colors arrive pre-scaled by intensity;
// falloff reaches 0 at the radius. u_pointLightCount <= MAX_POINT_LIGHTS.
const int MAX_POINT_LIGHTS = 4;
uniform int   u_pointLightCount;
uniform vec3  u_pointLightPos[MAX_POINT_LIGHTS];
uniform vec3  u_pointLightColor[MAX_POINT_LIGHTS];
uniform float u_pointLightRadius[MAX_POINT_LIGHTS];

out vec4 fragColor;

const float PI = 3.1415927;

// Sky hemisphere gradient (matches the GI pass), used for ambient and as the
// reflection ray's miss color.
vec3 skyRadiance(vec3 dir) {
    return mix(vec3(0.20, 0.22, 0.28), vec3(0.45, 0.55, 0.75), dir.y * 0.5 + 0.5);
}

// Per-voxel value hash for subtle albedo variation (keeps micro voxels legible).
float voxelHash(ivec3 c) {
    uint h = uint(c.x) * 374761393u + uint(c.y) * 668265263u + uint(c.z) * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return float(h & 0xFFFFu) / 65535.0;
}

struct Hit {
    float t;
    vec3  normal;
    uint  material;
    bool  hit;
};

// ── Minecraft-style per-pixel corner AO ─────────────────────────────────────
// Darkens hit points near solid neighbors of the hit face: the classic trick
// that makes micro voxel scenes read as detailed. 8 extra texel fetches, only
// on primary hits.

float voxelSolidAt(ivec3 c) {
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(u_gridDim)))) return 0.0;
    return texelFetch(u_volume, c, 0).r != 0u ? 1.0 : 0.0;
}

float cornerAO(float side1, float side2, float corner) {
    if (side1 > 0.5 && side2 > 0.5) return 0.0;
    return 1.0 - (side1 + side2 + corner) / 3.0;
}

float faceAO(ivec3 cell, vec3 normal, vec3 hitPos) {
    ivec3 n = ivec3(round(normal));
    ivec3 outside = cell + n;
    // The two axes spanning the hit face
    ivec3 t1 = (n.x != 0) ? ivec3(0, 1, 0) : ivec3(1, 0, 0);
    ivec3 t2 = (n.z != 0) ? ivec3(0, 1, 0) : ivec3(0, 0, 1);

    // Fractional position within the face
    vec3 f = (hitPos - u_volumeOrigin) / u_voxelSize - vec3(cell);
    float u = clamp(dot(f, vec3(t1)), 0.0, 1.0);
    float v = clamp(dot(f, vec3(t2)), 0.0, 1.0);

    float sP1 = voxelSolidAt(outside + t1), sM1 = voxelSolidAt(outside - t1);
    float sP2 = voxelSolidAt(outside + t2), sM2 = voxelSolidAt(outside - t2);
    float cPP = voxelSolidAt(outside + t1 + t2), cPM = voxelSolidAt(outside + t1 - t2);
    float cMP = voxelSolidAt(outside - t1 + t2), cMM = voxelSolidAt(outside - t1 - t2);

    float ao00 = cornerAO(sM1, sM2, cMM);
    float ao10 = cornerAO(sP1, sM2, cPM);
    float ao01 = cornerAO(sM1, sP2, cMP);
    float ao11 = cornerAO(sP1, sP2, cPP);
    return mix(mix(ao00, ao10, u), mix(ao01, ao11, u), v);
}

// Fine DDA over individual voxels inside one brick cell [lo, lo+B-1].
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

// Coarse DDA over bricks; descends into occupied bricks.
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

    // Entry face normal (used when the first cell tested is already solid).
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

void main() {
    // Reconstruct the world-space ray for this pixel from clip space.
    vec2 ndc = v_uv * 2.0 - 1.0;    // v_uv is 0..1 across the screen quad
    vec4 farH = u_invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = u_cameraPos;
    vec3 rd = normalize(farH.xyz / farH.w - ro);

    Hit h = raycast(ro, rd);
    if (!h.hit) {
        discard;
    }

    vec3 hitPos = ro + rd * h.t;
    ivec3 cell = clamp(ivec3(floor((hitPos + rd * u_voxelSize * 0.01 - u_volumeOrigin) / u_voxelSize)),
                       ivec3(0), ivec3(u_gridDim - 1));

    vec4 pal = texelFetch(u_palette, ivec2(int(h.material), 0), 0);
    vec3 albedo = pal.rgb;
    float emission = pal.a;    // 0..1 self-illumination (0 for normal materials)
    albedo *= 0.85 + 0.3 * voxelHash(cell);

    vec3 L = normalize(u_sunDir);
    float ndl = max(dot(h.normal, L), 0.0);

    float shadow = 1.0;
    if (u_shadowEnabled != 0 && ndl > 0.0) {
        vec3 so = hitPos + h.normal * u_voxelSize * 0.51;
        Hit sh = raycast(so, L);
        if (sh.hit) shadow = 0.0;
    }

    float ao = 1.0;
    if (u_aoStrength > 0.0) {
        ao = mix(1.0, faceAO(cell, h.normal, hitPos), u_aoStrength);
    }

    // Indirect light: traced 1-bounce GI when enabled (sky light + bounce,
    // so occlusion and color bleeding emerge naturally), else flat ambient.
    vec3 indirect;
    if (u_giStrength > 0.0) {
        indirect = texture(u_giTex, v_uv).rgb * u_giStrength;
    } else {
        vec3 skyAmbient = mix(vec3(0.20, 0.22, 0.28), vec3(0.45, 0.55, 0.75), h.normal.y * 0.5 + 0.5);
        indirect = skyAmbient * u_ambient;
    }

    // Local point lights: distance falloff + N·L, with an optional shadow ray
    // (same voxel DDA, gated on the shadow toggle to keep the cost bounded).
    vec3 pointLight = vec3(0.0);
    for (int i = 0; i < MAX_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 d = u_pointLightPos[i] - hitPos;
        float dist = length(d);
        float radius = u_pointLightRadius[i];
        if (dist >= radius) continue;
        vec3 Lp = d / max(dist, 1e-4);
        float pndl = max(dot(h.normal, Lp), 0.0);
        if (pndl <= 0.0) continue;
        float a = 1.0 - dist / radius;
        float atten = a * a;    // smooth falloff, exactly 0 at the radius
        float psh = 1.0;
        if (u_shadowEnabled != 0) {
            Hit sh = raycast(hitPos + h.normal * u_voxelSize * 0.51, Lp);
            if (sh.hit && sh.t < dist) psh = 0.0;
        }
        pointLight += u_pointLightColor[i] * (1.0 / PI) * pndl * atten * psh;
    }

    vec3 direct = u_sunColor * u_sunIntensity * (1.0 / PI) * ndl * shadow;
    // AO fully attenuates indirect; a stylized 30% also darkens direct so
    // corners stay readable in full sun (Teardown-ish look). Emission is
    // self-lit, added after AO so glowing voxels stay bright in their crevices.
    vec3 emissive = albedo * emission * u_emissiveStrength;
    vec3 color = albedo * (direct * (0.7 + 0.3 * ao) + indirect * ao + pointLight * ao) + emissive;

    // ── Per-material mirror reflections ─────────────────────────────────────
    // Reflective materials (crystal/ore/snow; palette row 1) cast one extra
    // reflection ray through the same DDA and blend the reflected radiance in
    // by a Schlick Fresnel term (F0 = reflectivity), so grazing angles read as
    // near-mirror. The reflected sample is cheaply shaded (sun + sky ambient +
    // emission) — enough to mirror the glowing orbs, terrain, and sky.
    float reflectivity = texelFetch(u_palette, ivec2(int(h.material), 1), 0).r;
    if (u_reflectionsEnabled != 0 && reflectivity > 0.0) {
        vec3 rdir = reflect(rd, h.normal);
        vec3 rorig = hitPos + h.normal * u_voxelSize * 0.51;
        Hit rh = raycast(rorig, rdir);
        vec3 refl;
        if (rh.hit) {
            vec4 rpal = texelFetch(u_palette, ivec2(int(rh.material), 0), 0);
            float rndl = max(dot(rh.normal, L), 0.0);
            refl = rpal.rgb * (u_sunColor * u_sunIntensity * (1.0 / PI) * rndl + skyRadiance(rh.normal) * u_ambient)
                 + rpal.rgb * rpal.a * u_emissiveStrength;
        } else {
            refl = skyRadiance(rdir);
        }
        float fres = reflectivity + (1.0 - reflectivity) * pow(1.0 - max(dot(-rd, h.normal), 0.0), 5.0);
        color = mix(color, refl, clamp(fres, 0.0, 1.0));
    }

    // Debug visualization of individual terms (keys in the MicroVoxel example)
    if (u_debugMode == 1) color = albedo;
    else if (u_debugMode == 2) color = h.normal * 0.5 + 0.5;
    else if (u_debugMode == 3) color = vec3(ao);
    else if (u_debugMode == 4) color = vec3(shadow);
    else if (u_debugMode == 5) color = texture(u_giTex, v_uv).rgb;
    else if (u_debugMode == 6) color = vec3(float(h.material) / 8.0);

    // Scene convention: gamma-encoded color into sceneRT (see pbr.frag); the
    // tonemap pass decodes with pow(2.2) first.
    fragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);

    vec4 clip = u_viewProj * vec4(hitPos, 1.0);
    float ndcDepth = clip.z / clip.w;           // GL clip space: -1..1
    gl_FragDepth = clamp(ndcDepth * 0.5 + 0.5, 0.0, 0.999999);
}

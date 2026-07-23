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
// The DDA runs in the volume's LOCAL space: the pixel's view ray is transformed
// by u_invObjModel (a rigid transform — rotation + translation, unit scale — so
// ray parameter t is a world distance too), marched against the axis-aligned
// local grid, and the hit is transformed back for shading/depth. This is what
// lets physics-driven volumes rotate freely while the DDA stays axis-aligned.
//
// The slab test uses u_boundsMin/u_boundsMax — the tight solid bounds from the
// occupancy grid — rather than the full grid box, so rays skip the volume's
// empty margins entirely (the rasterized box is shrunk to the same bounds).
//
// Storage (all CPU-built; edits re-upload affected regions):
//   u_volume    usampler3D  R8UI, gridDim^3, per-voxel palette index (0 = air)
//   u_occupancy usampler3D  R8UI, (gridDim/BRICK_DIM)^3, nonzero = brick solid
//   u_palette   sampler2D   256x2 RGBA8, albedo+emission / material params

#ifdef GL_ES
precision highp float;
precision highp int;
precision highp usampler3D;
precision highp sampler2D;
#endif

in vec3 v_worldPos;            // interpolated bounding-box surface position

uniform usampler3D u_volume;
uniform usampler3D u_occupancy;
uniform sampler2D  u_palette;
uniform sampler2D  u_giTex;    // accumulated 1-bounce indirect (microvoxel_gi.frag)

uniform mat4  u_viewProj;      // world -> clip (for depth output)
uniform vec2  u_viewportSize;  // pixels, for screen-space GI lookup
uniform vec3  u_cameraPos;     // world space
uniform mat4  u_objModel;      // volume local -> world (rigid: rotation + translation)
uniform mat4  u_invObjModel;   // world -> volume local
uniform vec3  u_volumeOrigin;  // LOCAL-space min corner of the grid
uniform vec3  u_boundsMin;     // LOCAL-space tight solid bounds (slab test)
uniform vec3  u_boundsMax;
uniform float u_voxelSize;     // world edge length of one voxel
uniform int   u_gridDim;       // voxels per volume edge (cubic)
uniform int   u_brickDim;      // voxels per brick edge (8)
uniform int   u_maxRaySteps;   // cap on coarse DDA iterations
uniform vec3  u_sunDir;        // world space, normalized, toward the sun
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform float u_ambient;
uniform int   u_shadowEnabled;
uniform float u_aoStrength;    // 0 disables corner AO
uniform float u_giStrength;    // 0 = flat ambient, >0 = traced indirect
uniform sampler2D u_giRaw;     // raw (un-denoised) GI, for the split-screen compare
uniform float u_giSplitX;      // <0 = off; else screen-x in [0,1]: left = raw, right = denoised
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
// reflection ray's miss color. Takes a WORLD-space direction.
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
    vec3  normal;   // LOCAL-space axis-aligned face normal
    uint  material;
    bool  hit;
};

// ── Minecraft-style per-pixel corner AO ─────────────────────────────────────
// Darkens hit points near solid neighbors of the hit face: the classic trick
// that makes micro voxel scenes read as detailed. 8 extra texel fetches, only
// on primary hits. All in local/voxel space.

float voxelSolidAt(ivec3 c) {
    if (any(lessThan(c, ivec3(0))) || any(greaterThanEqual(c, ivec3(u_gridDim)))) return 0.0;
    return texelFetch(u_volume, c, 0).r != 0u ? 1.0 : 0.0;
}

float cornerAO(float side1, float side2, float corner) {
    if (side1 > 0.5 && side2 > 0.5) return 0.0;
    return 1.0 - (side1 + side2 + corner) / 3.0;
}

float faceAO(ivec3 cell, vec3 normal, vec3 hitPosLocal) {
    ivec3 n = ivec3(round(normal));
    ivec3 outside = cell + n;
    // The two axes spanning the hit face
    ivec3 t1 = (n.x != 0) ? ivec3(0, 1, 0) : ivec3(1, 0, 0);
    ivec3 t2 = (n.z != 0) ? ivec3(0, 1, 0) : ivec3(0, 0, 1);

    // Fractional position within the face
    vec3 f = (hitPosLocal - u_volumeOrigin) / u_voxelSize - vec3(cell);
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

// Coarse DDA over bricks; descends into occupied bricks. Takes a LOCAL-space
// ray. The slab test clips against the tight solid bounds; brick cell indexing
// stays relative to the full grid origin.
Hit raycastLocal(vec3 ro, vec3 rd) {
    Hit r;
    r.hit = false; r.t = 0.0; r.normal = vec3(0.0); r.material = 0u;

    vec3 invDir = mix(vec3(1e30), 1.0 / rd, notEqual(rd, vec3(0.0)));

    vec3 tA = (u_boundsMin - ro) * invDir;
    vec3 tB = (u_boundsMax - ro) * invDir;
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
    ivec3 cell = clamp(ivec3(floor((p - u_volumeOrigin) / brickSize)), ivec3(0), brickGrid - 1);

    ivec3 stepDir = ivec3(sign(rd));
    vec3 tDelta = abs(vec3(brickSize) * invDir);
    vec3 stepPos = vec3(greaterThan(rd, vec3(0.0)));
    vec3 boundary = u_volumeOrigin + (vec3(cell) + stepPos) * brickSize;
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

// World-space ray -> local DDA. t is preserved by the rigid transform, so
// callers can keep reasoning in world distances.
Hit raycastWorld(vec3 roWorld, vec3 rdWorld) {
    vec3 ro = (u_invObjModel * vec4(roWorld, 1.0)).xyz;
    vec3 rd = normalize(mat3(u_invObjModel) * rdWorld);
    return raycastLocal(ro, rd);
}

void main() {
    // The bounding box was rasterized, so the view ray for this pixel goes from
    // the camera through this box-surface fragment. Screen uv (for the
    // screen-space GI texture) comes from the window-space fragment coordinate.
    vec2 v_uv = gl_FragCoord.xy / u_viewportSize;
    vec3 roW = u_cameraPos;
    vec3 rdW = normalize(v_worldPos - roW);

    // March in local space (t doubles as world distance — rigid transform).
    vec3 roL = (u_invObjModel * vec4(roW, 1.0)).xyz;
    vec3 rdL = normalize(mat3(u_invObjModel) * rdW);
    Hit h = raycastLocal(roL, rdL);
    if (!h.hit) {
        discard;
    }

    vec3 hitPosL = roL + rdL * h.t;
    vec3 hitPosW = (u_objModel * vec4(hitPosL, 1.0)).xyz;
    vec3 nW = normalize(mat3(u_objModel) * h.normal);// world normal for shading
    ivec3 cell = clamp(ivec3(floor((hitPosL + rdL * u_voxelSize * 0.01 - u_volumeOrigin) / u_voxelSize)),
                       ivec3(0), ivec3(u_gridDim - 1));

    vec4 pal = texelFetch(u_palette, ivec2(int(h.material), 0), 0);
    vec3 albedo = pal.rgb;
    float emission = pal.a;    // 0..1 self-illumination (0 for normal materials)
    albedo *= 0.85 + 0.3 * voxelHash(cell);

    vec3 L = normalize(u_sunDir);
    float ndl = max(dot(nW, L), 0.0);

    float shadow = 1.0;
    if (u_shadowEnabled != 0 && ndl > 0.0) {
        vec3 so = hitPosW + nW * u_voxelSize * 0.51;
        Hit sh = raycastWorld(so, L);
        if (sh.hit) shadow = 0.0;
    }

    float ao = 1.0;
    if (u_aoStrength > 0.0) {
        ao = mix(1.0, faceAO(cell, h.normal, hitPosL), u_aoStrength);
    }

    // Indirect light: traced 1-bounce GI when enabled (sky light + bounce,
    // so occlusion and color bleeding emerge naturally), else flat ambient.
    // The GI buffer only covers the nearest-K volumes the GI pass traced, so
    // fall back to flat ambient where the sample is invalid (alpha 0 = the GI
    // ray missed / this pixel's volume wasn't in the traced set).
    vec3 skyAmbient = skyRadiance(nW);
    vec3 indirect;
    if (u_giStrength > 0.0) {
        // Split-screen compare: left half samples the raw (un-denoised) GI,
        // right half the denoised GI, so the à-trous effect is visible in one
        // frame. u_giSplitX < 0 disables it (always denoised).
        vec4 gi = (u_giSplitX >= 0.0 && v_uv.x < u_giSplitX) ? texture(u_giRaw, v_uv)
                                                             : texture(u_giTex, v_uv);
        indirect = (gi.a > 0.0) ? gi.rgb * u_giStrength : skyAmbient * u_ambient;
    } else {
        indirect = skyAmbient * u_ambient;
    }

    // Local point lights: distance falloff + N·L, with an optional shadow ray
    // (same voxel DDA, gated on the shadow toggle to keep the cost bounded).
    vec3 pointLight = vec3(0.0);
    for (int i = 0; i < MAX_POINT_LIGHTS; i++) {
        if (i >= u_pointLightCount) break;
        vec3 d = u_pointLightPos[i] - hitPosW;
        float dist = length(d);
        float radius = u_pointLightRadius[i];
        if (dist >= radius) continue;
        vec3 Lp = d / max(dist, 1e-4);
        float pndl = max(dot(nW, Lp), 0.0);
        if (pndl <= 0.0) continue;
        float a = 1.0 - dist / radius;
        float atten = a * a;    // smooth falloff, exactly 0 at the radius
        float psh = 1.0;
        if (u_shadowEnabled != 0) {
            Hit sh = raycastWorld(hitPosW + nW * u_voxelSize * 0.51, Lp);
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
        vec3 rdirW = reflect(rdW, nW);
        vec3 rorigW = hitPosW + nW * u_voxelSize * 0.51;
        Hit rh = raycastWorld(rorigW, rdirW);
        vec3 refl;
        if (rh.hit) {
            vec4 rpal = texelFetch(u_palette, ivec2(int(rh.material), 0), 0);
            vec3 rnW = normalize(mat3(u_objModel) * rh.normal);
            float rndl = max(dot(rnW, L), 0.0);
            refl = rpal.rgb * (u_sunColor * u_sunIntensity * (1.0 / PI) * rndl + skyRadiance(rnW) * u_ambient)
                 + rpal.rgb * rpal.a * u_emissiveStrength;
        } else {
            refl = skyRadiance(rdirW);
        }
        float fres = reflectivity + (1.0 - reflectivity) * pow(1.0 - max(dot(-rdW, nW), 0.0), 5.0);
        color = mix(color, refl, clamp(fres, 0.0, 1.0));
    }

    // Debug visualization of individual terms (keys in the MicroVoxel example)
    if (u_debugMode == 1) color = albedo;
    else if (u_debugMode == 2) color = nW * 0.5 + 0.5;
    else if (u_debugMode == 3) color = vec3(ao);
    else if (u_debugMode == 4) color = vec3(shadow);
    else if (u_debugMode == 5)
        color = (u_giSplitX >= 0.0 && v_uv.x < u_giSplitX) ? texture(u_giRaw, v_uv).rgb : texture(u_giTex, v_uv).rgb;
    else if (u_debugMode == 6) color = vec3(float(h.material) / 8.0);

    // Split-screen divider so the raw|denoised boundary is legible.
    if (u_giSplitX >= 0.0 && abs(v_uv.x - u_giSplitX) < 0.0012) color = vec3(1.0);

    // Scene convention: gamma-encoded color into sceneRT (see pbr.frag); the
    // tonemap pass decodes with pow(2.2) first.
    fragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);

    vec4 clip = u_viewProj * vec4(hitPosW, 1.0);
    float ndcDepth = clip.z / clip.w;           // GL clip space: -1..1
    gl_FragDepth = clamp(ndcDepth * 0.5 + 0.5, 0.0, 0.999999);
}

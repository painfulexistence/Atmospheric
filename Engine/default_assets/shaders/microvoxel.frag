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

in vec3 v_worldPos;            // interpolated bounding-box surface position

uniform usampler3D u_volume;
uniform usampler3D u_occupancy;
uniform sampler2D  u_palette;
uniform sampler2D  u_giTex;    // accumulated 1-bounce indirect (microvoxel_gi.frag)

uniform mat4  u_viewProj;      // world -> clip (for depth output)
uniform vec2  u_viewportSize;  // pixels, for screen-space GI lookup
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
// reflection ray's miss color.
vec3 skyRadiance(vec3 dir) {
    return mix(vec3(0.20, 0.22, 0.28), vec3(0.45, 0.55, 0.75), dir.y * 0.5 + 0.5);
}

// Interleaved gradient noise — a stable per-pixel [0,1) used by the glossy
// jitter. A fixed dither pattern (no temporal accumulation on this pass) reads
// better than white noise.
float ign(vec2 px) {
    return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}

// Glossy jitter: tilt `dir` inside a roughness^2-scaled cone (the palette byte
// row 1 always reserved for this). Clamped back above the surface so a
// jittered reflection never dives through its own face.
vec3 glossyDir(vec3 dir, vec3 faceN, float roughness, vec2 px) {
    if (roughness < 0.02) return dir;
    float u1 = ign(px) - 0.5;
    float u2 = ign(px + vec2(17.0, 31.0)) - 0.5;
    vec3 t1 = normalize(cross(dir, abs(dir.y) < 0.98 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 t2 = cross(dir, t1);
    float cone = roughness * roughness * 0.6;
    vec3 j = normalize(dir + (t1 * u1 + t2 * u2) * cone);
    float below = dot(j, faceN);
    if (below < 0.02) j = normalize(j + faceN * (0.02 - below));
    return j;
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

// Radiance along a secondary ray (reflection or the scene behind glass):
// opaque hit shaded with the same cheap sun + sky + emission the reflection
// path always used; miss returns sky. One bounce only.
vec3 secondaryRadiance(vec3 ro, vec3 rd) {
    Hit rh = raycast(ro, rd);
    if (rh.hit) {
        vec4 rpal = texelFetch(u_palette, ivec2(int(rh.material), 0), 0);
        float rndl = max(dot(rh.normal, normalize(u_sunDir)), 0.0);
        return rpal.rgb * (u_sunColor * u_sunIntensity * (1.0 / PI) * rndl + skyRadiance(rh.normal) * u_ambient)
             + rpal.rgb * rpal.a * u_emissiveStrength;
    }
    return skyRadiance(rd);
}

// Transmission byte (palette row 1 .b) of a cell's material; -1 for air.
float voxelTransmission(ivec3 cell) {
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(u_gridDim)))) return -1.0;
    uint mat = texelFetch(u_volume, cell, 0).r;
    if (mat == 0u) return -1.0;
    return texelFetch(u_palette, ivec2(int(mat), 1), 0).b;
}

// The BTDF: march THROUGH the transmissive medium voxel-by-voxel from the
// entry hit, accumulating the in-glass path length for Beer-Lambert, then bend
// out at the first glass->air face (total internal reflection continues
// straight — the one-bounce approximation) and gather the scene behind with
// the normal DDA. An opaque voxel inside the medium terminates the march and
// is shaded directly. `mediumAlbedo` tints the absorption.
vec3 transmitRadiance(vec3 entryPos, vec3 tdir, float ior, vec3 mediumAlbedo) {
    const int MAX_GLASS_STEPS = 128;
    const float BEER_K = 3.0;  // absorption strength per meter

    vec3 local = entryPos - u_volumeOrigin;
    vec3 p = local + tdir * (u_voxelSize * 1e-3);
    ivec3 cell = ivec3(floor(p / u_voxelSize));
    ivec3 stepDir = ivec3(sign(tdir));
    vec3 invD = 1.0 / tdir;
    vec3 tDelta = abs(vec3(u_voxelSize) * invD);
    vec3 stepPos = vec3(greaterThan(tdir, vec3(0.0)));
    vec3 tMax = ((vec3(cell) + stepPos) * u_voxelSize - local) * invD;
    float t = 0.0;
    vec3 exitN = vec3(0.0);

    for (int i = 0; i < MAX_GLASS_STEPS; i++) {
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x += tDelta.x; cell.x += stepDir.x;
            exitN = vec3(-float(stepDir.x), 0.0, 0.0);
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y += tDelta.y; cell.y += stepDir.y;
            exitN = vec3(0.0, -float(stepDir.y), 0.0);
        } else {
            t = tMax.z; tMax.z += tDelta.z; cell.z += stepDir.z;
            exitN = vec3(0.0, 0.0, -float(stepDir.z));
        }
        float trans = voxelTransmission(cell);
        if (trans > 0.001) continue;  // still inside the medium (stacked glass blends)

        vec3 beer = exp(-(vec3(1.0) - mediumAlbedo) * BEER_K * t);
        bool inBounds = all(greaterThanEqual(cell, ivec3(0))) && all(lessThan(cell, ivec3(u_gridDim)));
        uint mat = inBounds ? texelFetch(u_volume, cell, 0).r : 0u;
        if (mat != 0u) {
            // Opaque voxel embedded in / behind the glass: shade it at this face.
            vec4 mpal = texelFetch(u_palette, ivec2(int(mat), 0), 0);
            float mndl = max(dot(exitN, normalize(u_sunDir)), 0.0);
            vec3 lit = mpal.rgb * (u_sunColor * u_sunIntensity * (1.0 / PI) * mndl + skyRadiance(exitN) * u_ambient)
                     + mpal.rgb * mpal.a * u_emissiveStrength;
            return lit * beer;
        }
        // Glass -> air: bend out (eta = ior). TIR keeps the direction — the
        // cheap approximation instead of bouncing back into the medium.
        vec3 outDir = refract(tdir, exitN, ior);
        if (dot(outDir, outDir) < 1e-6) outDir = tdir;
        vec3 exitPos = entryPos + tdir * t;
        return secondaryRadiance(exitPos + outDir * (u_voxelSize * 0.51), outDir) * beer;
    }
    return skyRadiance(tdir) * exp(-(vec3(1.0) - mediumAlbedo) * BEER_K * t);
}

void main() {
    // The bounding box was rasterized, so the view ray for this pixel goes from
    // the camera through this box-surface fragment. Screen uv (for the
    // screen-space GI texture) comes from the window-space fragment coordinate.
    vec2 v_uv = gl_FragCoord.xy / u_viewportSize;
    vec3 ro = u_cameraPos;
    vec3 rd = normalize(v_worldPos - ro);

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
        // Split-screen compare: left half samples the raw (un-denoised) GI,
        // right half the denoised GI, so the à-trous effect is visible in one
        // frame. u_giSplitX < 0 disables it (always denoised).
        vec3 gi = (u_giSplitX >= 0.0 && v_uv.x < u_giSplitX) ? texture(u_giRaw, v_uv).rgb
                                                             : texture(u_giTex, v_uv).rgb;
        indirect = gi * u_giStrength;
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
    // corners stay readable in full sun (Teardown-ish look).
    vec3 color = albedo * (direct * (0.7 + 0.3 * ao) + indirect * ao + pointLight * ao);

    // ── Secondary rays: glossy reflection + dielectric transmission ─────────
    // Palette row 1 = reflectivity.r, roughness.g, transmission.b, ior.a
    // (ior decodes to 1.0 + a, so 1.0..2.0). Reflective materials cast one
    // roughness-jittered reflection ray; transmissive materials (glass) run
    // the full BTDF — Fresnel from the IOR splits the energy into that
    // reflection and a refraction ray that marches through the medium with
    // Beer-Lambert absorption, bends out at the far face, and gathers the
    // scene behind (see transmitRadiance).
    vec4 mparams = texelFetch(u_palette, ivec2(int(h.material), 1), 0);
    float reflectivity = mparams.r;
    float roughness = mparams.g;
    float transmission = mparams.b;
    float ior = 1.0 + mparams.a;
    if (u_reflectionsEnabled != 0 && (transmission > 0.001 || reflectivity > 0.0)) {
        float cosI = max(dot(-rd, h.normal), 0.0);
        vec3 rdir = glossyDir(reflect(rd, h.normal), h.normal, roughness, gl_FragCoord.xy);
        vec3 refl = secondaryRadiance(hitPos + h.normal * u_voxelSize * 0.51, rdir);
        if (transmission > 0.001) {
            float f0 = (ior - 1.0) / (ior + 1.0);
            f0 *= f0;
            float F = f0 + (1.0 - f0) * pow(1.0 - cosI, 5.0);
            vec3 tdir = refract(rd, h.normal, 1.0 / ior);
            vec3 trans;
            if (dot(tdir, tdir) < 1e-6) {
                trans = refl;  // grazing entry TIR: everything reflects
            } else {
                tdir = glossyDir(tdir, -h.normal, roughness, gl_FragCoord.xy + vec2(7.0, 13.0));  // frosted
                trans = transmitRadiance(hitPos, tdir, ior, pal.rgb);
            }
            vec3 glass = F * refl + (1.0 - F) * trans;
            color = mix(color, glass, transmission);
        } else {
            float fres = reflectivity + (1.0 - reflectivity) * pow(1.0 - cosI, 5.0);
            color = mix(color, refl, clamp(fres, 0.0, 1.0));
        }
    }
    // Emission after the secondary mix: an emissive surface glows regardless
    // of how reflective/transmissive it is (the crystal keeps its inner light).
    color += albedo * emission * u_emissiveStrength;

    // Debug visualization of individual terms (keys in the MicroVoxel example)
    if (u_debugMode == 1) color = albedo;
    else if (u_debugMode == 2) color = h.normal * 0.5 + 0.5;
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

    vec4 clip = u_viewProj * vec4(hitPos, 1.0);
    float ndcDepth = clip.z / clip.w;           // GL clip space: -1..1
    gl_FragDepth = clamp(ndcDepth * 0.5 + 0.5, 0.0, 0.999999);
}

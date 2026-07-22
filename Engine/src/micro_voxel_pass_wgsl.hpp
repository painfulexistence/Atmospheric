#pragma once
// Internal WGSL shader source for micro_voxel_pass.cpp.
// Only #include this file from micro_voxel_pass.cpp.
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)

// ── Micro Voxel Raymarch (experimental) — WebGPU path ─────────────────────────
// WGSL port of default_assets/shaders/microvoxel.{vert,frag}. Same two-level
// DDA over a dense voxel volume (texture_3d<u32>) + coarse occupancy texture.
// Depth is written via @builtin(frag_depth) WITHOUT the GL -1..1 -> 0..1 remap:
// the WebGPU rasterizer writes clip.z/clip.w directly, so voxel depth matches
// the rasterized scene only if it does the same. gridDim.w carries brickDim;
// misc = (maxRaySteps, shadowEnabled, reflectionsEnabled, pointLightCount).
// Emissive, point lights, and reflections have GL parity; traced GI is GL-only.
static const char* MICROVOXEL_WGSL = R"(
struct Uniforms {
    invViewProj:  mat4x4<f32>,
    viewProj:     mat4x4<f32>,
    cameraPos:    vec4<f32>,   // xyz
    originVoxel:  vec4<f32>,   // volumeOrigin.xyz, voxelSize
    sunDirInt:    vec4<f32>,   // sunDir.xyz, sunIntensity
    sunColAmb:    vec4<f32>,   // sunColor.xyz, ambient
    gridDim:      vec4<i32>,   // gridDim.xyz, brickDim
    misc:         vec4<i32>,   // maxRaySteps, shadowEnabled, reflectionsEnabled, pointLightCount
    params:       vec4<f32>,   // aoStrength, emissiveStrength, _, _
    pointPosRadius: array<vec4<f32>, 4>,   // xyz = world pos, w = radius
    pointColor:     array<vec4<f32>, 4>,   // rgb = color pre-scaled by intensity
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var volume:    texture_3d<u32>;
@group(1) @binding(1) var occupancy: texture_3d<u32>;
@group(1) @binding(2) var palette:   texture_2d<f32>;

const PI: f32 = 3.1415927;

// Sky hemisphere gradient — ambient + reflection-ray miss color.
fn skyRadiance(dir: vec3<f32>) -> vec3<f32> {
    return mix(vec3<f32>(0.20, 0.22, 0.28), vec3<f32>(0.45, 0.55, 0.75), dir.y * 0.5 + 0.5);
}

struct VOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex fn vs(@builtin(vertex_index) vid: u32) -> VOut {
    let x = f32((vid << 1u) & 2u);
    let y = f32(vid & 2u);
    var out: VOut;
    out.pos = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    out.uv  = vec2<f32>(x, y);
    return out;
}

struct Hit {
    t: f32,
    normal: vec3<f32>,
    material: u32,
    hit: bool,
};

fn voxelHash(c: vec3<i32>) -> f32 {
    var h: u32 = u32(c.x) * 374761393u + u32(c.y) * 668265263u + u32(c.z) * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return f32(h & 0xFFFFu) / 65535.0;
}

fn stepPos(rd: vec3<f32>) -> vec3<f32> {
    return select(vec3<f32>(0.0), vec3<f32>(1.0), rd > vec3<f32>(0.0));
}

// Minecraft-style per-pixel corner AO (see microvoxel.frag).
fn voxelSolidAt(c: vec3<i32>) -> f32 {
    if (any(c < vec3<i32>(0)) || any(c >= u.gridDim.xyz)) { return 0.0; }
    return select(0.0, 1.0, textureLoad(volume, c, 0).r != 0u);
}

fn cornerAO(side1: f32, side2: f32, corner: f32) -> f32 {
    if (side1 > 0.5 && side2 > 0.5) { return 0.0; }
    return 1.0 - (side1 + side2 + corner) / 3.0;
}

fn faceAO(cell: vec3<i32>, normal: vec3<f32>, hitPos: vec3<f32>) -> f32 {
    let n = vec3<i32>(round(normal));
    let outside = cell + n;
    var t1 = vec3<i32>(1, 0, 0);
    if (n.x != 0) { t1 = vec3<i32>(0, 1, 0); }
    var t2 = vec3<i32>(0, 0, 1);
    if (n.z != 0) { t2 = vec3<i32>(0, 1, 0); }

    let f = (hitPos - u.originVoxel.xyz) / u.originVoxel.w - vec3<f32>(cell);
    let uu = clamp(dot(f, vec3<f32>(t1)), 0.0, 1.0);
    let vv = clamp(dot(f, vec3<f32>(t2)), 0.0, 1.0);

    let sP1 = voxelSolidAt(outside + t1); let sM1 = voxelSolidAt(outside - t1);
    let sP2 = voxelSolidAt(outside + t2); let sM2 = voxelSolidAt(outside - t2);
    let cPP = voxelSolidAt(outside + t1 + t2); let cPM = voxelSolidAt(outside + t1 - t2);
    let cMP = voxelSolidAt(outside - t1 + t2); let cMM = voxelSolidAt(outside - t1 - t2);

    let ao00 = cornerAO(sM1, sM2, cMM);
    let ao10 = cornerAO(sP1, sM2, cPM);
    let ao01 = cornerAO(sM1, sP2, cMP);
    let ao11 = cornerAO(sP1, sP2, cPP);
    return mix(mix(ao00, ao10, uu), mix(ao01, ao11, uu), vv);
}

fn traverseBrick(ro: vec3<f32>, rd: vec3<f32>, invDir: vec3<f32>, tStart: f32,
                 brickCell: vec3<i32>, enterNormal: vec3<f32>) -> Hit {
    var r: Hit;
    r.hit = false; r.t = tStart; r.normal = enterNormal; r.material = 0u;

    let origin = u.originVoxel.xyz;
    let vsize = u.originVoxel.w;
    let bd = u.gridDim.w;
    let lo = brickCell * bd;
    let hi = lo + bd - 1;

    let eps = vsize * 1e-3;
    let p = ro + rd * (tStart + eps);
    var cell = clamp(vec3<i32>(floor((p - origin) / vsize)), lo, hi);

    let stepDir = vec3<i32>(sign(rd));
    let tDelta = abs(vec3<f32>(vsize) * invDir);
    let boundary = origin + (vec3<f32>(cell) + stepPos(rd)) * vsize;
    var tMax = select(vec3<f32>(1e30), (boundary - ro) * invDir, rd != vec3<f32>(0.0));

    var t = tStart;
    var normal = enterNormal;
    let iters = 3 * bd + 1;

    for (var i = 0; i < iters; i = i + 1) {
        let mat = textureLoad(volume, cell, 0).r;
        if (mat != 0u) {
            r.hit = true; r.t = t; r.normal = normal; r.material = mat;
            return r;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x = tMax.x + tDelta.x; cell.x = cell.x + stepDir.x;
            normal = vec3<f32>(-f32(stepDir.x), 0.0, 0.0);
            if (cell.x < lo.x || cell.x > hi.x) { break; }
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y = tMax.y + tDelta.y; cell.y = cell.y + stepDir.y;
            normal = vec3<f32>(0.0, -f32(stepDir.y), 0.0);
            if (cell.y < lo.y || cell.y > hi.y) { break; }
        } else {
            t = tMax.z; tMax.z = tMax.z + tDelta.z; cell.z = cell.z + stepDir.z;
            normal = vec3<f32>(0.0, 0.0, -f32(stepDir.z));
            if (cell.z < lo.z || cell.z > hi.z) { break; }
        }
    }
    return r;
}

fn raycast(ro: vec3<f32>, rd: vec3<f32>) -> Hit {
    var r: Hit;
    r.hit = false; r.t = 0.0; r.normal = vec3<f32>(0.0); r.material = 0u;

    let origin = u.originVoxel.xyz;
    let vsize = u.originVoxel.w;
    let gdim = u.gridDim.xyz;
    let bd = u.gridDim.w;

    let invDir = select(vec3<f32>(1e30), vec3<f32>(1.0) / rd, rd != vec3<f32>(0.0));
    let bmin = origin;
    let bmax = bmin + vec3<f32>(gdim) * vsize;

    let tA = (bmin - ro) * invDir;
    let tB = (bmax - ro) * invDir;
    let tNear = min(tA, tB);
    let tFar  = max(tA, tB);
    let tEnter = max(max(tNear.x, tNear.y), tNear.z);
    let tExit  = min(min(tFar.x, tFar.y), tFar.z);
    if (tExit <= max(tEnter, 0.0)) { return r; }

    var enterNormal: vec3<f32>;
    if (tEnter > 0.0) {
        if (tNear.x > tNear.y && tNear.x > tNear.z) { enterNormal = vec3<f32>(-sign(rd.x), 0.0, 0.0); }
        else if (tNear.y > tNear.z)                 { enterNormal = vec3<f32>(0.0, -sign(rd.y), 0.0); }
        else                                        { enterNormal = vec3<f32>(0.0, 0.0, -sign(rd.z)); }
    } else {
        let a = abs(rd);
        if (a.x > a.y && a.x > a.z) { enterNormal = vec3<f32>(-sign(rd.x), 0.0, 0.0); }
        else if (a.y > a.z)         { enterNormal = vec3<f32>(0.0, -sign(rd.y), 0.0); }
        else                        { enterNormal = vec3<f32>(0.0, 0.0, -sign(rd.z)); }
    }

    let brickSize = vsize * f32(bd);
    let brickGrid = gdim / bd;

    var t = max(tEnter, 0.0);
    let eps = vsize * 1e-3;
    let p = ro + rd * (t + eps);
    var cell = clamp(vec3<i32>(floor((p - bmin) / brickSize)), vec3<i32>(0), brickGrid - 1);

    let stepDir = vec3<i32>(sign(rd));
    let tDelta = abs(vec3<f32>(brickSize) * invDir);
    let boundary = bmin + (vec3<f32>(cell) + stepPos(rd)) * brickSize;
    var tMax = select(vec3<f32>(1e30), (boundary - ro) * invDir, rd != vec3<f32>(0.0));

    var normal = enterNormal;
    let maxSteps = u.misc.x;

    for (var i = 0; i < maxSteps; i = i + 1) {
        if (textureLoad(occupancy, cell, 0).r != 0u) {
            let h = traverseBrick(ro, rd, invDir, t, cell, normal);
            if (h.hit) { return h; }
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x = tMax.x + tDelta.x; cell.x = cell.x + stepDir.x;
            normal = vec3<f32>(-f32(stepDir.x), 0.0, 0.0);
            if (cell.x < 0 || cell.x >= brickGrid.x) { break; }
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y = tMax.y + tDelta.y; cell.y = cell.y + stepDir.y;
            normal = vec3<f32>(0.0, -f32(stepDir.y), 0.0);
            if (cell.y < 0 || cell.y >= brickGrid.y) { break; }
        } else {
            t = tMax.z; tMax.z = tMax.z + tDelta.z; cell.z = cell.z + stepDir.z;
            normal = vec3<f32>(0.0, 0.0, -f32(stepDir.z));
            if (cell.z < 0 || cell.z >= brickGrid.z) { break; }
        }
        if (t > tExit) { break; }
    }
    return r;
}

// Interleaved gradient noise — a stable per-pixel [0,1) for the glossy jitter
// (no temporal accumulation on this pass, so a fixed pattern beats white noise).
fn ign(px: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}

// Glossy jitter: tilt `dir` inside a roughness^2-scaled cone (palette row 1's
// long-reserved byte). Clamped back above the surface.
fn glossyDir(dir: vec3<f32>, faceN: vec3<f32>, roughness: f32, px: vec2<f32>) -> vec3<f32> {
    if (roughness < 0.02) { return dir; }
    let u1 = ign(px) - 0.5;
    let u2 = ign(px + vec2<f32>(17.0, 31.0)) - 0.5;
    let axis = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), abs(dir.y) < 0.98);
    let t1 = normalize(cross(dir, axis));
    let t2 = cross(dir, t1);
    let cone = roughness * roughness * 0.6;
    var j = normalize(dir + (t1 * u1 + t2 * u2) * cone);
    let below = dot(j, faceN);
    if (below < 0.02) { j = normalize(j + faceN * (0.02 - below)); }
    return j;
}

// Radiance along a secondary ray (reflection or the scene behind glass):
// cheap sun + sky + emission shading on hit, sky on miss. One bounce only.
fn secondaryRadiance(ro: vec3<f32>, rd: vec3<f32>) -> vec3<f32> {
    let rh = raycast(ro, rd);
    if (rh.hit) {
        let rpal = textureLoad(palette, vec2<i32>(i32(rh.material), 0), 0);
        let L = normalize(u.sunDirInt.xyz);
        let rndl = max(dot(rh.normal, L), 0.0);
        return rpal.rgb * (u.sunDirInt.w * u.sunColAmb.xyz * (1.0 / PI) * rndl + skyRadiance(rh.normal) * u.sunColAmb.w)
             + rpal.rgb * rpal.a * u.params.y;
    }
    return skyRadiance(rd);
}

// Transmission byte (palette row 1 .b) of a cell's material; -1 for air.
fn voxelTransmission(cell: vec3<i32>) -> f32 {
    if (any(cell < vec3<i32>(0)) || any(cell >= u.gridDim.xyz)) { return -1.0; }
    let mat = textureLoad(volume, cell, 0).r;
    if (mat == 0u) { return -1.0; }
    return textureLoad(palette, vec2<i32>(i32(mat), 1), 0).b;
}

// The BTDF: march THROUGH the transmissive medium voxel-by-voxel, accumulate
// the in-glass path length for Beer-Lambert, bend out at the first glass->air
// face (TIR continues straight — the one-bounce approximation), then gather
// the scene behind with the normal DDA. Opaque voxels inside the medium
// terminate the march and shade directly. `mediumAlbedo` tints the absorption.
fn transmitRadiance(entryPos: vec3<f32>, tdirIn: vec3<f32>, ior: f32, mediumAlbedo: vec3<f32>) -> vec3<f32> {
    let MAX_GLASS_STEPS = 128;
    let BEER_K = 3.0;
    let tdir = tdirIn;
    let vsize = u.originVoxel.w;
    let origin = u.originVoxel.xyz;

    let local = entryPos - origin;
    let p = local + tdir * (vsize * 1e-3);
    var cell = vec3<i32>(floor(p / vsize));
    let stepDir = vec3<i32>(sign(tdir));
    let invD = select(vec3<f32>(1e30), vec3<f32>(1.0) / tdir, tdir != vec3<f32>(0.0));
    let tDelta = abs(vec3<f32>(vsize) * invD);
    let boundary = (vec3<f32>(cell) + stepPos(tdir)) * vsize;
    var tMax = select(vec3<f32>(1e30), (boundary - local) * invD, tdir != vec3<f32>(0.0));
    var t = 0.0;
    var exitN = vec3<f32>(0.0);

    for (var i = 0; i < MAX_GLASS_STEPS; i = i + 1) {
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; tMax.x = tMax.x + tDelta.x; cell.x = cell.x + stepDir.x;
            exitN = vec3<f32>(-f32(stepDir.x), 0.0, 0.0);
        } else if (tMax.y < tMax.z) {
            t = tMax.y; tMax.y = tMax.y + tDelta.y; cell.y = cell.y + stepDir.y;
            exitN = vec3<f32>(0.0, -f32(stepDir.y), 0.0);
        } else {
            t = tMax.z; tMax.z = tMax.z + tDelta.z; cell.z = cell.z + stepDir.z;
            exitN = vec3<f32>(0.0, 0.0, -f32(stepDir.z));
        }
        let trans = voxelTransmission(cell);
        if (trans > 0.001) { continue; }  // still inside the medium

        let beer = exp(-(vec3<f32>(1.0) - mediumAlbedo) * BEER_K * t);
        let inBounds = all(cell >= vec3<i32>(0)) && all(cell < u.gridDim.xyz);
        var mat = 0u;
        if (inBounds) { mat = textureLoad(volume, cell, 0).r; }
        if (mat != 0u) {
            // Opaque voxel embedded in / behind the glass: shade at this face.
            let mpal = textureLoad(palette, vec2<i32>(i32(mat), 0), 0);
            let L = normalize(u.sunDirInt.xyz);
            let mndl = max(dot(exitN, L), 0.0);
            let lit = mpal.rgb * (u.sunDirInt.w * u.sunColAmb.xyz * (1.0 / PI) * mndl + skyRadiance(exitN) * u.sunColAmb.w)
                    + mpal.rgb * mpal.a * u.params.y;
            return lit * beer;
        }
        // Glass -> air: bend out (eta = ior). TIR keeps the direction.
        var outDir = refract(tdir, exitN, ior);
        if (dot(outDir, outDir) < 1e-6) { outDir = tdir; }
        let exitPos = entryPos + tdir * t;
        return secondaryRadiance(exitPos + outDir * (vsize * 0.51), outDir) * beer;
    }
    return skyRadiance(tdir) * exp(-(vec3<f32>(1.0) - mediumAlbedo) * BEER_K * t);
}

struct FOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};

@fragment fn fs(in: VOut) -> FOut {
    var out: FOut;
    out.color = vec4<f32>(0.0);
    out.depth = 1.0;

    let ndc = vec2<f32>(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0);
    let farH = u.invViewProj * vec4<f32>(ndc, 1.0, 1.0);
    let ro = u.cameraPos.xyz;
    let rd = normalize(farH.xyz / farH.w - ro);

    let h = raycast(ro, rd);
    if (!h.hit) {
        discard;
        return out;
    }

    let vsize = u.originVoxel.w;
    let hitPos = ro + rd * h.t;
    let cell = clamp(vec3<i32>(floor((hitPos + rd * vsize * 0.01 - u.originVoxel.xyz) / vsize)),
                     vec3<i32>(0), u.gridDim.xyz - 1);

    let pal = textureLoad(palette, vec2<i32>(i32(h.material), 0), 0);
    var albedo = pal.rgb;
    let emission = pal.a;    // 0..1 self-illumination (0 for normal materials)
    albedo = albedo * (0.85 + 0.3 * voxelHash(cell));

    let L = normalize(u.sunDirInt.xyz);
    let ndl = max(dot(h.normal, L), 0.0);

    var shadow = 1.0;
    if (u.misc.y != 0 && ndl > 0.0) {
        let so = hitPos + h.normal * vsize * 0.51;
        let sh = raycast(so, L);
        if (sh.hit) { shadow = 0.0; }
    }

    var ao = 1.0;
    if (u.params.x > 0.0) {
        ao = mix(1.0, faceAO(cell, h.normal, hitPos), u.params.x);
    }

    let skyAmbient = skyRadiance(h.normal);

    // Local point lights: distance falloff + N·L, optional shadow ray (gated on
    // the shadow toggle). Colors arrive pre-scaled by intensity.
    var pointLight = vec3<f32>(0.0);
    for (var i = 0; i < 4; i = i + 1) {
        if (i >= u.misc.w) { break; }
        let d = u.pointPosRadius[i].xyz - hitPos;
        let dist = length(d);
        let radius = u.pointPosRadius[i].w;
        if (dist >= radius) { continue; }
        let Lp = d / max(dist, 1e-4);
        let pndl = max(dot(h.normal, Lp), 0.0);
        if (pndl <= 0.0) { continue; }
        let a = 1.0 - dist / radius;
        let atten = a * a;
        var psh = 1.0;
        if (u.misc.y != 0) {
            let sh = raycast(hitPos + h.normal * vsize * 0.51, Lp);
            if (sh.hit && sh.t < dist) { psh = 0.0; }
        }
        pointLight = pointLight + u.pointColor[i].xyz * (1.0 / PI) * pndl * atten * psh;
    }

    let direct = u.sunDirInt.w * u.sunColAmb.xyz * (1.0 / PI) * ndl * shadow;
    // AO fully attenuates ambient; a stylized 30% also darkens direct.
    // (The WebGPU path keeps flat ambient + self-emission; traced GI is GL-only,
    // matching the GI split. Point lights and secondary rays have parity here.)
    var color = albedo * (direct * (0.7 + 0.3 * ao) + skyAmbient * u.sunColAmb.w * ao + pointLight * ao);

    // ── Secondary rays: glossy reflection + dielectric transmission ─────────
    // Palette row 1 = reflectivity.r, roughness.g, transmission.b, ior.a
    // (ior decodes to 1.0 + a). GL parity: reflective materials cast one
    // roughness-jittered ray; glass runs the Fresnel-split BTDF with
    // Beer-Lambert absorption (see transmitRadiance).
    let mparams = textureLoad(palette, vec2<i32>(i32(h.material), 1), 0);
    let reflectivity = mparams.r;
    let roughness = mparams.g;
    let transmission = mparams.b;
    let ior = 1.0 + mparams.a;
    if (u.misc.z != 0 && (transmission > 0.001 || reflectivity > 0.0)) {
        let px = in.uv * vec2<f32>(4096.0, 4096.0);  // uv-derived jitter seed (no frag coord here)
        let cosI = max(dot(-rd, h.normal), 0.0);
        let rdir = glossyDir(reflect(rd, h.normal), h.normal, roughness, px);
        let refl = secondaryRadiance(hitPos + h.normal * vsize * 0.51, rdir);
        if (transmission > 0.001) {
            var f0 = (ior - 1.0) / (ior + 1.0);
            f0 = f0 * f0;
            let F = f0 + (1.0 - f0) * pow(1.0 - cosI, 5.0);
            let tdir0 = refract(rd, h.normal, 1.0 / ior);
            var trans: vec3<f32>;
            if (dot(tdir0, tdir0) < 1e-6) {
                trans = refl;  // grazing entry TIR: everything reflects
            } else {
                let tdir = glossyDir(tdir0, -h.normal, roughness, px + vec2<f32>(7.0, 13.0));  // frosted
                trans = transmitRadiance(hitPos, tdir, ior, pal.rgb);
            }
            let glass = F * refl + (1.0 - F) * trans;
            color = mix(color, glass, transmission);
        } else {
            let fres = reflectivity + (1.0 - reflectivity) * pow(1.0 - cosI, 5.0);
            color = mix(color, refl, clamp(fres, 0.0, 1.0));
        }
    }
    // Emission after the secondary mix: an emissive surface glows regardless
    // of how reflective/transmissive it is (the crystal keeps its inner light).
    color = color + albedo * emission * u.params.y;

    // Scene convention: gamma-encoded into sceneRT (see FORWARD_OPAQUE_WGSL);
    // the tonemap pass decodes with pow(2.2) first.
    out.color = vec4<f32>(pow(max(color, vec3<f32>(0.0)), vec3<f32>(1.0 / 2.2)), 1.0);

    let clip = u.viewProj * vec4<f32>(hitPos, 1.0);
    out.depth = clamp(clip.z / clip.w, 0.0, 0.999999);   // WebGPU: no -1..1 remap
    return out;
}
)";

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

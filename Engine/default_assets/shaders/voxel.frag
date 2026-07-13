#version 410 core

// GLES/WebGL2 needs explicit precision, and sampler3D (u_giRadiance, added for
// VoxelGI/VXAO) has no default precision there. Declaring any precision here
// also opts this shader out of the loader's auto-injection, so cover float/int
// too. (Desktop core profile ignores this block.)
#ifdef GL_ES
precision highp float;
precision highp int;
precision highp sampler3D;
#endif

in vec3 v_worldPos;
in vec3 v_normal;
flat in uint v_voxelId;
flat in uint v_faceId;
in float v_ao;// baked corner AO, 0 (occluded) .. 1 (open)

uniform vec3  u_lightDir;
uniform vec3  u_lightColor;
uniform vec3  u_ambientColor;
uniform vec3  u_fogColor;
uniform float u_fogDensity;
uniform vec3  u_cameraPos;
uniform float u_aoStrength;  // 0 disables corner AO, 1 = full

uniform int   u_paletteIndex;  // 0-5, default 4 (VX Palette 5)

// World-space clip plane (n, d) used by PlanarReflectionPass to cut away
// geometry below the mirror plane; all-zero disables (dot == 0 is kept).
uniform vec4  u_clipPlane;

// Global illumination: 0 = off, 1 = SSGI (reserved), 2 = VoxelGI (VCT).
// The VoxelConeGI radiance grid: rgb = injected radiance, a = opacity. Placed
// in world space by origin (min corner) + dim cells of cellSize metres.
uniform int       u_giMode;
uniform float     u_giStrength;
uniform float     u_vxaoStrength; // 0 disables VXAO (voxel cone-traced AO)
uniform sampler3D u_giRadiance;
uniform vec3      u_giOrigin;
uniform float     u_giCellSize;
uniform int       u_giDim;
uniform float     u_giMaxMip;

out vec4 fragColor;

// Trace one diffuse cone through the radiance grid, accumulating front-to-back
// and stepping the mip level up with the cone's widening footprint.
vec3 coneTrace(vec3 startWorld, vec3 dir, float aperture) {
    float gridSize = float(u_giDim) * u_giCellSize;
    vec4 acc = vec4(0.0);
    float dist = u_giCellSize * 1.5;// skip the origin voxel
    for (int i = 0; i < 32; ++i) {
        if (acc.a >= 0.98 || dist >= gridSize) break;
        float diameter = max(u_giCellSize, 2.0 * aperture * dist);
        float mip = clamp(log2(diameter / u_giCellSize), 0.0, u_giMaxMip);
        vec3 wp = startWorld + dir * dist;
        vec3 uvw = (wp - u_giOrigin) / gridSize;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;
        vec4 s = textureLod(u_giRadiance, uvw, mip);
        acc.rgb += (1.0 - acc.a) * s.a * s.rgb;
        acc.a += (1.0 - acc.a) * s.a;
        dist += diameter * 0.5;
    }
    return acc.rgb;
}

// Five-cone diffuse gather over the hemisphere around the surface normal.
vec3 voxelGI(vec3 worldPos, vec3 n) {
    vec3 start = worldPos + n * u_giCellSize * 1.5;
    vec3 up = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    float aperture = 0.577;// ~60° cone
    vec3 gi = coneTrace(start, n, aperture);
    gi += coneTrace(start, normalize(n * 0.5 + t), aperture);
    gi += coneTrace(start, normalize(n * 0.5 - t), aperture);
    gi += coneTrace(start, normalize(n * 0.5 + b), aperture);
    gi += coneTrace(start, normalize(n * 0.5 - b), aperture);
    return gi / 5.0;
}

// Accumulate opacity along one cone for occlusion (VXAO). Unlike coneTrace this
// keeps only the alpha and weights nearer occluders more (broad-cavity AO, not
// long-range shadowing), stopping after a few metres.
float coneOcclusion(vec3 startWorld, vec3 dir, float aperture) {
    float gridSize = float(u_giDim) * u_giCellSize;
    float maxDist = 6.0 * u_giCellSize;// AO is a local effect
    float occ = 0.0;
    float dist = u_giCellSize * 1.5;
    for (int i = 0; i < 16; ++i) {
        if (occ >= 0.98 || dist >= maxDist) break;
        float diameter = max(u_giCellSize, 2.0 * aperture * dist);
        float mip = clamp(log2(diameter / u_giCellSize), 0.0, u_giMaxMip);
        vec3 wp = startWorld + dir * dist;
        vec3 uvw = (wp - u_giOrigin) / gridSize;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;
        float a = textureLod(u_giRadiance, uvw, mip).a;
        occ += (1.0 - occ) * a / (1.0 + dist);// distance falloff
        dist += diameter * 0.5;
    }
    return occ;
}

// Five-cone occlusion gather -> AO factor (1 = fully open, 0 = occluded).
float voxelAO(vec3 worldPos, vec3 n) {
    vec3 start = worldPos + n * u_giCellSize * 1.5;
    vec3 up = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    float aperture = 0.577;
    float occ = coneOcclusion(start, n, aperture);
    occ += coneOcclusion(start, normalize(n * 0.5 + t), aperture);
    occ += coneOcclusion(start, normalize(n * 0.5 - t), aperture);
    occ += coneOcclusion(start, normalize(n * 0.5 + b), aperture);
    occ += coneOcclusion(start, normalize(n * 0.5 - b), aperture);
    return clamp(1.0 - occ / 5.0, 0.0, 1.0);
}

// Cosine colour palette: a + b * cos(2π * (c*t + d))
// All 6 palettes ported from VX shaders/chunk.vert
vec3 palette(float t) {
    vec3 a, b, c, d;
    switch (u_paletteIndex) {
        case 0: // Palette 1 — warm pink/gold
            a = vec3(0.800, 0.150, 0.560); b = vec3(0.610, 0.300, 0.120);
            c = vec3(0.640, 0.100, 0.590); d = vec3(0.380, 0.860, 0.470); break;
        case 1: // Palette 2 — cool blue/purple
            a = vec3(0.288, 0.303, 0.466); b = vec3(0.806, 0.664, 0.998);
            c = vec3(1.253, 0.992, 1.569); d = vec3(3.379, 3.574, 3.026); break;
        case 2: // Palette 3 — earthy green
            a = vec3(0.420, 0.696, 0.625); b = vec3(0.791, 0.182, 0.271);
            c = vec3(0.368, 0.650, 0.103); d = vec3(0.913, 3.624, 0.320); break;
        case 3: // Palette 4 — forest
            a = vec3(0.427, 0.346, 0.372); b = vec3(0.288, 0.918, 0.336);
            c = vec3(0.635, 1.136, 0.404); d = vec3(1.893, 0.663, 1.910); break;
        case 5: // Palette 6 — vivid mint/coral
            a = vec3(0.686, 0.933, 0.933); b = vec3(0.957, 0.643, 0.957);
            c = vec3(0.867, 0.627, 0.867); d = vec3(1.961, 2.871, 1.702); break;
        default: // Palette 5 — soft cool blue-grey (VX default)
            a = vec3(0.746, 0.815, 0.846); b = vec3(0.195, 0.283, 0.187);
            c = vec3(1.093, 1.417, 1.405); d = vec3(5.435, 2.400, 5.741); break;
    }
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    if (dot(u_clipPlane.xyz, v_worldPos) + u_clipPlane.w < 0.0) {
        discard;
    }

    vec3 norm = normalize(v_normal);

    float diff = max(dot(norm, normalize(u_lightDir)), 0.0);

    vec3 baseColor = palette(float(v_voxelId) / 50.0);

    // Face shading levels matching VX: top=1.0, bottom=0.5, sides=0.9
    float faceShade = (v_faceId == 0u) ? 1.0 : (v_faceId == 1u ? 0.5 : 0.9);
    baseColor *= faceShade;

    // Corner AO darkens creases/contact edges. Remap the 0..1 level to a
    // gentler [1-strength .. 1] range so open faces stay full-bright.
    float ao = mix(1.0, v_ao, u_aoStrength);
    // VXAO adds broad concavity (caves, pits) the per-vertex corner term can't
    // see, cone-traced from the same grid's opacity. Stacks with corner AO.
    if (u_vxaoStrength > 0.0) {
        ao *= mix(1.0, voxelAO(v_worldPos, norm), u_vxaoStrength);
    }

    vec3 ambient = u_ambientColor * baseColor;
    vec3 diffuse = diff * u_lightColor * baseColor;
    vec3 color   = (ambient + diffuse) * ao;

    // VoxelGI (VCT): add cone-traced indirect bounce, modulated by receiver
    // albedo and the corner AO. SSGI (mode 1) is not wired yet — no-op.
    if (u_giMode == 2) {
        vec3 indirect = voxelGI(v_worldPos, norm);
        color += u_giStrength * indirect * baseColor * ao;
    }

    float dist      = length(v_worldPos - u_cameraPos);
    float fogFactor = clamp(exp(-u_fogDensity * dist), 0.0, 1.0);
    color = mix(u_fogColor, color, fogFactor);

    fragColor = vec4(color, 1.0);
}

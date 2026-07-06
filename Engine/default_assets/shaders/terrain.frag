#version 410

// Lit terrain surface built around WorldCreator/Gaea export workflows.
// All maps are optional:
//   base_map    full-terrain color map (0-1 UV)
//   normal_map  full-terrain normal map; overrides heightmap-derived normals
//   ao_map      full-terrain ambient occlusion
//   splat_map   RGBA weights for up to 4 tiled detail layers
//   layerN_*    tiled detail albedo / tangent-space normal per layer
// Fallbacks: layers > base_map > legacy height palette (now lit). Without a
// splat map, layer weights derive from slope/height (0 = ground, 1 = steep
// rock, 2 = high snow).

struct SurfaceParams
{
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;
    float shininess;
};

struct DirLight
{
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float intensity;
    int cast_shadow;
    mat4 ProjectionView;
};

uniform SurfaceParams surf_params;
uniform DirLight main_light;
uniform vec3 cam_pos;

uniform sampler2D height_map_unit;
uniform sampler2D base_map_unit;
uniform sampler2D normal_map_unit;
uniform sampler2D ao_map_unit;
uniform sampler2D splat_map_unit;
uniform sampler2D layer0_albedo_unit;
uniform sampler2D layer1_albedo_unit;
uniform sampler2D layer2_albedo_unit;
uniform sampler2D layer3_albedo_unit;
uniform sampler2D layer0_normal_unit;
uniform sampler2D layer1_normal_unit;
uniform sampler2D layer2_normal_unit;
uniform sampler2D layer3_normal_unit;

uniform float height_scale;
uniform float world_size;
uniform int palette_index;  // 0-5, default 0 (warm pink/gold — legacy terrain color)
uniform int has_base_map;
uniform int has_normal_map;
uniform int has_ao_map;
uniform int has_splat_map;
uniform int layer_count;
uniform vec3 fog_color;    // linear-space aerial perspective color
uniform float fog_density; // 0 = off
uniform float layer_tiling[4];
uniform float layer_has_normal[4];

// World-space clip plane (n, d) used by PlanarReflectionPass to cut away
// geometry below the mirror plane; all-zero disables (dot == 0 is kept).
uniform vec4 u_clipPlane;

in vec2 frag_uv;
in vec3 frag_pos;
in float height;

layout(location = 0) out vec4 Color;

const float gamma = 2.2;

// Cosine colour palette: a + b * cos(2π * (c*t + d)), kept as the no-maps
// fallback. The 6 palettes match VoxelWorld (voxel.frag); index 0 is the
// legacy terrain color so palette_index=0 reproduces the original look.
vec3 palette(float t) {
    vec3 a, b, c, d;
    switch (palette_index) {
        case 1: // Palette 2 — cool blue/purple
            a = vec3(0.288, 0.303, 0.466); b = vec3(0.806, 0.664, 0.998);
            c = vec3(1.253, 0.992, 1.569); d = vec3(3.379, 3.574, 3.026); break;
        case 2: // Palette 3 — earthy green
            a = vec3(0.420, 0.696, 0.625); b = vec3(0.791, 0.182, 0.271);
            c = vec3(0.368, 0.650, 0.103); d = vec3(0.913, 3.624, 0.320); break;
        case 3: // Palette 4 — forest
            a = vec3(0.427, 0.346, 0.372); b = vec3(0.288, 0.918, 0.336);
            c = vec3(0.635, 1.136, 0.404); d = vec3(1.893, 0.663, 1.910); break;
        case 4: // Palette 5 — soft cool blue-grey
            a = vec3(0.746, 0.815, 0.846); b = vec3(0.195, 0.283, 0.187);
            c = vec3(1.093, 1.417, 1.405); d = vec3(5.435, 2.400, 5.741); break;
        case 5: // Palette 6 — vivid mint/coral
            a = vec3(0.686, 0.933, 0.933); b = vec3(0.957, 0.643, 0.957);
            c = vec3(0.867, 0.627, 0.867); d = vec3(1.961, 2.871, 1.702); break;
        default: // Palette 1 — warm pink/gold (legacy terrain default)
            a = vec3(0.800, 0.150, 0.560); b = vec3(0.610, 0.300, 0.120);
            c = vec3(0.640, 0.100, 0.590); d = vec3(0.380, 0.860, 0.470); break;
    }
    return a + b * cos(6.28318 * (c * t + d));
}

// World-space normal from heightmap central differences. Mesh UV maps u -> +X
// and v -> +Z across world_size, so the derivation matches the displaced
// geometry exactly.
vec3 HeightmapNormal(vec2 uv)
{
    vec2 ts = 1.0 / vec2(textureSize(height_map_unit, 0));
    float hl = textureLod(height_map_unit, uv - vec2(ts.x, 0.0), 0.0).r;
    float hr = textureLod(height_map_unit, uv + vec2(ts.x, 0.0), 0.0).r;
    float hd = textureLod(height_map_unit, uv - vec2(0.0, ts.y), 0.0).r;
    float hu = textureLod(height_map_unit, uv + vec2(0.0, ts.y), 0.0).r;
    float dhdx = (hr - hl) * height_scale / (2.0 * ts.x * world_size);
    float dhdz = (hu - hd) * height_scale / (2.0 * ts.y * world_size);
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}

vec3 TerrainBaseNormal()
{
    if (has_normal_map == 1) {
        // Terrain-plane tangent space (u -> +X, v -> +Z, up -> +Y).
        vec3 n = texture(normal_map_unit, frag_uv).rgb * 2.0 - 1.0;
        return normalize(vec3(n.x, n.z, n.y));
    }
    return HeightmapNormal(frag_uv);
}

vec4 LayerWeights(vec3 n, float h)
{
    if (has_splat_map == 1) {
        vec4 w = texture(splat_map_unit, frag_uv);
        if (layer_count < 4) w.a = 0.0;
        if (layer_count < 3) w.b = 0.0;
        if (layer_count < 2) w.g = 0.0;
        return w / max(w.r + w.g + w.b + w.a, 1e-4);
    }
    // Automatic slope/height weights.
    float slope = 1.0 - n.y;
    float wRock = layer_count > 1 ? smoothstep(0.2, 0.45, slope) : 0.0;
    float wSnow = layer_count > 2 ? smoothstep(0.65, 0.8, h) * (1.0 - wRock) : 0.0;
    float wBase = max(1.0 - wRock - wSnow, 0.0);
    return vec4(wBase, wRock, wSnow, 0.0);
}

// Detail layers tile in world space (frag_pos.xz / world_size wraps every
// world_size metres) so the pattern phase is continuous across streamed
// terrain tiles instead of restarting at each tile's UV origin.
vec2 LayerUV()
{
    return frag_pos.xz / world_size;
}

vec3 BlendLayerAlbedo(vec4 w)
{
    vec2 uv = LayerUV();
    vec3 c = vec3(0.0);
    if (layer_count > 0) c += w.r * pow(texture(layer0_albedo_unit, uv * layer_tiling[0]).rgb, vec3(gamma));
    if (layer_count > 1) c += w.g * pow(texture(layer1_albedo_unit, uv * layer_tiling[1]).rgb, vec3(gamma));
    if (layer_count > 2) c += w.b * pow(texture(layer2_albedo_unit, uv * layer_tiling[2]).rgb, vec3(gamma));
    if (layer_count > 3) c += w.a * pow(texture(layer3_albedo_unit, uv * layer_tiling[3]).rgb, vec3(gamma));
    return c;
}

vec3 BlendLayerNormal(vec4 w)
{
    vec2 uv = LayerUV();
    vec3 flat_n = vec3(0.0, 0.0, 1.0);
    vec3 n = vec3(0.0);
    if (layer_count > 0) n += w.r * (layer_has_normal[0] > 0.5 ? texture(layer0_normal_unit, uv * layer_tiling[0]).rgb * 2.0 - 1.0 : flat_n);
    if (layer_count > 1) n += w.g * (layer_has_normal[1] > 0.5 ? texture(layer1_normal_unit, uv * layer_tiling[1]).rgb * 2.0 - 1.0 : flat_n);
    if (layer_count > 2) n += w.b * (layer_has_normal[2] > 0.5 ? texture(layer2_normal_unit, uv * layer_tiling[2]).rgb * 2.0 - 1.0 : flat_n);
    if (layer_count > 3) n += w.a * (layer_has_normal[3] > 0.5 ? texture(layer3_normal_unit, uv * layer_tiling[3]).rgb * 2.0 - 1.0 : flat_n);
    return normalize(n + vec3(0.0, 0.0, 1e-4));
}

// Perturb the base normal by a tangent-space detail normal.
vec3 PerturbNormal(vec3 N, vec3 detail)
{
    vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
    vec3 B = cross(N, T);
    return normalize(T * detail.x + B * detail.y + N * detail.z);
}

void main()
{
    if (dot(u_clipPlane.xyz, frag_pos) + u_clipPlane.w < 0.0) {
        discard;
    }

    vec3 N = TerrainBaseNormal();

    vec3 albedo;
    if (layer_count > 0) {
        vec4 w = LayerWeights(N, height);
        albedo = BlendLayerAlbedo(w) * surf_params.diffuse;
        N = PerturbNormal(N, BlendLayerNormal(w));
    } else if (has_base_map == 1) {
        albedo = pow(texture(base_map_unit, frag_uv).rgb, vec3(gamma)) * surf_params.diffuse;
    } else {
        albedo = palette(height);
    }

    float ao = has_ao_map == 1 ? texture(ao_map_unit, frag_uv).r : 1.0;

    vec3 lightDir = normalize(-main_light.direction);
    float ndl = clamp(dot(N, lightDir), 0.0, 1.0);
    vec3 lit = albedo * main_light.diffuse * ndl;
    lit += vec3(0.2) * ao * albedo;  // fixed ambient term, matching pbr.frag

    // Aerial perspective: exponential fade toward the horizon color. Reads
    // distance as distance — without it far mountains look like near hills.
    if (fog_density > 0.0) {
        float fogF = 1.0 - exp(-fog_density * distance(frag_pos, cam_pos));
        lit = mix(lit, fog_color, fogF);
    }

    Color = vec4(pow(lit, vec3(1.0 / gamma)), 1.0);
}

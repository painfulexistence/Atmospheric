#version 410

#define MAX_NUM_AUX_LIGHTS 6
#define SHADOW_KERNEL_LEVEL 1

struct SurfaceParams
{
    vec3 diffuse;
    vec3 specular;
    vec3 ambient;
    float shininess;
};

struct Surface
{
    vec3 color;
    float ao;
    float roughness;
    float metallic;
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

struct PointLight
{
    vec3 position;
    vec3 attenuation;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float intensity;
    int cast_shadow;
    mat4 ProjectionViews[6];
};

layout(location = 0) out vec4 Color;

uniform SurfaceParams surf_params;
uniform DirLight main_light;
uniform PointLight aux_lights[MAX_NUM_AUX_LIGHTS];
uniform int aux_light_count;
uniform vec3 cam_pos;
uniform sampler2D base_map_unit;
uniform sampler2D normal_map_unit;
uniform sampler2D ao_map_unit;
uniform sampler2D roughness_map_unit;
uniform sampler2D metallic_map_unit;
uniform float u_roughnessFactor;// PBRMaterial scalar; scales the roughness map
uniform float u_metallicFactor; // PBRMaterial scalar; scales the metallic map
uniform int u_useBlinnPhong;// 1 -> BlinnPhongMaterial (legacy specular); 0 -> PBR Cook-Torrance
uniform sampler2D shadow_map_unit;
uniform samplerCube omni_shadow_map_unit;
uniform float time;

// Image-based lighting: equirectangular HDR environment (Renderer::environmentMap).
// u_useEnv gates it (0 -> flat ambient fallback); u_envMaxLod is the top mip level
// used as the fully-blurred (roughest / diffuse) sample. Sampling matches skybox.frag.
uniform sampler2D u_envMap;
uniform int u_useEnv;
uniform float u_envMaxLod;
uniform float u_iblDiffuse;// debug scale on the diffuse IBL term (default 1.0)
uniform float u_iblSpecular;// debug scale on the specular IBL term (default 1.0)

// World-space clip plane (n, d) used by PlanarReflectionPass to cut away
// geometry below the mirror plane; all-zero disables (dot == 0 is kept).
uniform vec4 u_clipPlane;

in vec3 frag_pos;
in vec2 tex_uv;
in mat3 TBN;


const float PI = 3.1415927;
const float gamma = 2.2;

vec3 BlinnPhongBRDF(vec3 norm, vec3 lightDir, vec3 viewDir, Surface surf);

vec3 CookTorranceBRDF(vec3 norm, vec3 lightDir, vec3 viewDir, Surface surf);

float TrowbridgeReitzGGX(float nh, float r);

float SmithsSchlickGGX(float nv, float nl, float r);

vec3 FresnelSchlick(float nh, vec3 f0);

vec3 SurfaceColor(vec3 base);

float ShadowBias(vec3 norm, vec3 lightDir);

float DirectionalShadow(vec3 shadowCoords, float bias);

float PointShadow(vec3 shadowCoords, float bias);

// Dispatch to the active shading model. BlinnPhongMaterial sets u_useBlinnPhong.
vec3 EvalBRDF(vec3 norm, vec3 lightDir, vec3 viewDir, Surface surf) {
    return (u_useBlinnPhong == 1) ? BlinnPhongBRDF(norm, lightDir, viewDir, surf)
                                  : CookTorranceBRDF(norm, lightDir, viewDir, surf);
}

vec3 CalculateDirectionalLight(DirLight light, vec3 norm, vec3 viewDir, Surface surf) {
    vec3 lightDir = normalize(-light.direction);

    vec4 lightSpaceFragPos = light.ProjectionView * vec4(frag_pos, 1.0);
    vec3 lightSpaceFragCoords = lightSpaceFragPos.xyz / lightSpaceFragPos.w;
    float shadow = float(light.cast_shadow) * DirectionalShadow(lightSpaceFragCoords * 0.5 + 0.5, ShadowBias(norm, lightDir));
    vec3 radiance = light.diffuse * clamp(1.0 - shadow, 0.0, 1.0);

    return EvalBRDF(norm, lightDir, viewDir, surf) * radiance * clamp(dot(norm, lightDir), 0.0, 1.0);
}

vec3 CalculatePointLight(PointLight light, vec3 norm, vec3 viewDir, Surface surf) {
    vec3 lightDir = normalize(light.position - frag_pos);

    float dist = distance(light.position, frag_pos);
    float attenuation = 1.0 / (dist * dist);
    float shadow = float(light.cast_shadow) * PointShadow((frag_pos - light.position) / 400.0f, ShadowBias(norm, lightDir));
    // intensity scales the photometric power (e.g. Quake light values arrive
    // normalized to 300 == 1.0); without it every point light was pinned to
    // the raw color at 1m, far too dim to read at room distances.
    vec3 radiance = attenuation * light.intensity * light.diffuse * clamp(1.0 - shadow, 0.0, 1.0);

    return EvalBRDF(norm, lightDir, viewDir, surf) * radiance * clamp(dot(norm, lightDir), 0.0, 1.0);
}

vec3 BlinnPhongBRDF(vec3 norm, vec3 lightDir, vec3 viewDir, Surface surf) {
    // NOTES: the light.specular/light.diffuse term was extracted out to render equation
    vec3 halfway = normalize(lightDir + viewDir);
    float nl = clamp(dot(norm, lightDir), 0.0, 1.0);
    float nh = clamp(dot(norm, halfway), 0.0, 1.0);

    vec3 diffuse = nl * surf.color;
    vec3 specular = pow(nh, surf_params.shininess) * smoothstep(0.0, 0.2, dot(norm, lightDir)) * surf_params.specular;

    return diffuse + specular;
}

vec3 CookTorranceBRDF(vec3 norm, vec3 lightDir, vec3 viewDir, Surface surf) {
    vec3 halfway = normalize(lightDir + viewDir);
    float nv = clamp(dot(norm, viewDir), 0.0, 1.0);
    float nl = clamp(dot(norm, lightDir), 0.0, 1.0);
    float nh = clamp(dot(norm, halfway), 0.0, 1.0);
    float vh = clamp(dot(viewDir, halfway), 0.0, 1.0);

    float D = TrowbridgeReitzGGX(nh, surf.roughness + 0.01);
    float G = SmithsSchlickGGX(nv, nl, surf.roughness + 0.01);
    vec3 F = FresnelSchlick(vh, mix(vec3(0.04), surf.color, surf.metallic));

    vec3 specular = D * F * G / max(4.0 * nv * nl, 0.0001);
    vec3 ks = F;
    vec3 kd = (1.0 - surf.metallic) * (vec3(1.0) - ks);
    vec3 diffuse = kd * surf.color / PI;

    return diffuse + specular;
}

float TrowbridgeReitzGGX(float nh, float r) {
    float r2 = r * r;
    float a2 = r2 * r2;
    float nh2 = nh * nh;
    float nhr2 = (nh2 * (a2 - 1.0) + 1.0) * (nh2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * nhr2);
}

float SmithsSchlickGGX(float nv, float nl, float r) {
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    float ggx1 = nv / (nv * (1.0 - k) + k);
    float ggx2 = nl / (nl * (1.0 - k) + k);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float vh, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
}

// Fresnel with a roughness term so grazing angles on rough surfaces don't
// over-brighten the environment reflection (Sébastien Lagarde's formulation).
vec3 FresnelSchlickRoughness(float ct, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - ct, 0.0, 1.0), 5.0);
}

// atan(z, x) / asin(y) equirectangular mapping — identical to skybox.frag so the
// reflected environment lines up with the drawn sky.
vec2 dirToEquirectUV(vec3 dir) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
    return uv * vec2(0.1591549, 0.3183099) + 0.5;
}

// Split-sum-style IBL approximated with the environment map's own mip chain:
// the top mip stands in for the diffuse irradiance, and roughness selects a
// specular LOD. No dedicated irradiance / prefilter / BRDF-LUT passes (yet).
vec3 ComputeIBL(vec3 norm, vec3 viewDir, Surface surf) {
    float nv = clamp(dot(norm, viewDir), 0.0, 1.0);
    vec3 f0 = mix(vec3(0.04), surf.color, surf.metallic);
    vec3 F = FresnelSchlickRoughness(nv, f0, surf.roughness);
    vec3 kd = (1.0 - F) * (1.0 - surf.metallic);

    // Diffuse: heavily-blurred environment (top mip ~ average) tinted by albedo.
    vec3 irradiance = textureLod(u_envMap, dirToEquirectUV(norm), u_envMaxLod).rgb;
    vec3 diffuse = irradiance * surf.color;

    // Specular: reflection vector, LOD scaled by roughness.
    vec3 R = reflect(-viewDir, norm);
    vec3 prefiltered = textureLod(u_envMap, dirToEquirectUV(R), surf.roughness * u_envMaxLod).rgb;
    vec3 specular = prefiltered * F;

    // u_iblDiffuse / u_iblSpecular are debug/tuning scales (default 1.0). Lower
    // the diffuse term to stop a strongly-tinted env map from washing albedo.
    return (kd * diffuse * u_iblDiffuse + specular * u_iblSpecular) * surf.ao;
}

vec3 SurfaceColor(vec3 base) {
    vec3 texColor = pow(texture(base_map_unit, tex_uv).rgb, vec3(gamma));
    return base * texColor;
}

float SurfaceAO() {
    return texture(ao_map_unit, tex_uv).r;
}

float SurfaceRoughness() {
    return clamp(texture(roughness_map_unit, tex_uv).r * u_roughnessFactor, 0.0, 1.0);
}

float SurfaceMetallic() {
    return clamp(texture(metallic_map_unit, tex_uv).r * u_metallicFactor, 0.0, 1.0);
}

float ShadowBias(vec3 norm, vec3 lightDir) {
    return 0.0025 * (1.0 - abs(1.0 - 2.0 * abs(dot(norm, lightDir)))); //maximize when dot(norm, lightDir) = 0.5
}

float DirectionalShadow(vec3 shadowCoords, float bias) {
    float depth = shadowCoords.z;

    float shadow = 0.0;
    if (depth <= 1.0)
    {
        int samples = 0;
        float texelSize = 1.0 / float(textureSize(shadow_map_unit, 0).x);
        for(int dx = -SHADOW_KERNEL_LEVEL; dx <= SHADOW_KERNEL_LEVEL; ++dx)
        {
            for(int dy = -SHADOW_KERNEL_LEVEL; dy <= SHADOW_KERNEL_LEVEL; ++dy)
            {
                vec2 samplePoint = shadowCoords.xy + texelSize * vec2(dx, dy);
                shadow += (depth - bias >= texture(shadow_map_unit, samplePoint).r) ? 1.0 : 0.0;
                ++samples;
            }
        }
        shadow /= float(samples);
    }
    return shadow;
}

float PointShadow(vec3 shadowCoords, float bias) {
    float depth = length(shadowCoords);

    float shadow = 0.0;
    if (depth <= 1.0)
    {
        int samples = 0;
        float texelSize = 1.0 / float(textureSize(omni_shadow_map_unit, 0).x);
        for(int dx = -SHADOW_KERNEL_LEVEL; dx <= SHADOW_KERNEL_LEVEL; ++dx)
        {
            for(int dy = -SHADOW_KERNEL_LEVEL; dy <= SHADOW_KERNEL_LEVEL; ++dy)
            {
                for(int dz = -SHADOW_KERNEL_LEVEL; dz <= SHADOW_KERNEL_LEVEL; ++dz)
                {
                    vec3 samplePoint = shadowCoords + texelSize * vec3(dx, dy, dz);
                    shadow += (depth - bias >= texture(omni_shadow_map_unit, samplePoint).r) ? 1.0 : 0.0;
                    ++samples;
                }
            }
        }
        shadow /= float(samples);
    }
    return shadow;
}

void main() {
    if (dot(u_clipPlane.xyz, frag_pos) + u_clipPlane.w < 0.0) {
        discard;
    }

    vec3 texNorm = texture(normal_map_unit, tex_uv).rgb * 2.0 - 1.0;
    vec3 norm = normalize(TBN * texNorm);
    vec3 viewDir = normalize(cam_pos - frag_pos);

    Surface surf = Surface(
        SurfaceColor(surf_params.diffuse),
        SurfaceAO(),
        SurfaceRoughness(),
        SurfaceMetallic()
    );

    vec3 result = vec3(0.0);
    result += CalculateDirectionalLight(main_light, norm, viewDir, surf);
    for (int i = 0; i < aux_light_count && i < MAX_NUM_AUX_LIGHTS; i++) {
        result += CalculatePointLight(aux_lights[i], norm, viewDir, surf);
    }
    if (u_useEnv == 1) {
        result += ComputeIBL(norm, viewDir, surf);
    } else {
        result += vec3(0.2) * surf.ao * surf.color;
    }

    result = pow(result, vec3(1.0 / gamma));

    Color = vec4(result, 1.0);
}

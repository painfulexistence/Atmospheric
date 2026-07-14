#version 330 core

layout (location = 0) out vec3 g_position;
layout (location = 1) out vec3 g_normal;
layout (location = 2) out vec4 g_albedo;
layout (location = 3) out vec3 g_material;

in vec3 frag_pos;
in vec2 tex_uv;
in mat3 TBN;

uniform sampler2D baseMap;
uniform sampler2D normalMap;
uniform sampler2D roughnessMap;
uniform sampler2D metallicMap;
uniform sampler2D aoMap;
// PBRMaterial params, glTF semantics: value = map * factor; an absent map
// counts as white so the factor stands alone (has-map flags gate the sample).
uniform float u_roughnessFactor;
uniform float u_metallicFactor;
uniform int u_hasRoughnessMap;
uniform int u_hasMetallicMap;

void main() {
    g_position = frag_pos;

    vec3 normal = texture(normalMap, tex_uv).rgb;
    normal = normal * 2.0 - 1.0;
    g_normal = normalize(TBN * normal);

    g_albedo = texture(baseMap, tex_uv);

    float roughness = (u_hasRoughnessMap == 1) ? texture(roughnessMap, tex_uv).r : 1.0;
    float metallic = (u_hasMetallicMap == 1) ? texture(metallicMap, tex_uv).r : 1.0;
    g_material.r = clamp(roughness * u_roughnessFactor, 0.0, 1.0);
    g_material.g = clamp(metallic * u_metallicFactor, 0.0, 1.0);
    g_material.b = texture(aoMap, tex_uv).r;
}
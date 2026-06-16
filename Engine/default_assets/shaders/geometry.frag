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

void main() {
    g_position = frag_pos;

    vec3 normal = texture(normalMap, tex_uv).rgb;
    normal = normal * 2.0 - 1.0;
    g_normal = normalize(TBN * normal);

    g_albedo = texture(baseMap, tex_uv);

    g_material.r = texture(roughnessMap, tex_uv).r;
    g_material.g = texture(metallicMap, tex_uv).r;
    g_material.b = texture(aoMap, tex_uv).r;
}
#version 330 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;
layout (location = 5) in mat4 m_world;

out vec3 frag_pos;
out vec2 tex_uv;
out mat3 TBN;

uniform mat4 ProjectionView;

void main() {
    frag_pos = vec3(m_world * vec4(position, 1.0));
    tex_uv = uv;

    mat3 normalMatrix = transpose(inverse(mat3(m_world)));
    vec3 T = normalize(normalMatrix * tangent);
    vec3 N = normalize(normalMatrix * normal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);

    gl_Position = ProjectionView * vec4(frag_pos, 1.0);
}
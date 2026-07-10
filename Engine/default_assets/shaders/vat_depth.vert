#version 410

// VAT-aware directional shadow depth. Mirrors depth_simple.vert but displaces
// the vertex from the animation texture first, so a VAT mesh casts a shadow
// that matches its animated silhouette instead of the rest pose. Only position
// is needed for a depth write. See vat.vert for the full decode rationale.

uniform mat4 ProjectionView;

layout(location = 0) in vec3 position;
layout(location = 5) in mat4 World;

uniform sampler2D vat_position_map;
uniform int   vat_vert_count;
uniform int   vat_frame_count;
uniform float vat_time;
uniform int   vat_enabled;

vec3 vatPosition(vec3 fallback) {
    if (vat_enabled != 1 || vat_vert_count <= 0 || vat_frame_count <= 0) return fallback;
    float u = (float(gl_VertexID) + 0.5) / float(vat_vert_count);
    float ft = vat_time * float(max(vat_frame_count - 1, 0));
    float f0 = floor(ft);
    float f1 = min(f0 + 1.0, float(vat_frame_count - 1));
    float fr = ft - f0;
    float v0 = (f0 + 0.5) / float(vat_frame_count);
    float v1 = (f1 + 0.5) / float(vat_frame_count);
    vec3 p0 = textureLod(vat_position_map, vec2(u, v0), 0.0).rgb;
    vec3 p1 = textureLod(vat_position_map, vec2(u, v1), 0.0).rgb;
    return mix(p0, p1, fr);
}

void main() {
    gl_Position = ProjectionView * World * vec4(vatPosition(position), 1.0);
}

#version 410

uniform sampler2D color_map_unit;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

vec3 sepia(vec3 c) {
    return vec3(
        dot(c, vec3(0.393, 0.769, 0.189)),
        dot(c, vec3(0.349, 0.686, 0.168)),
        dot(c, vec3(0.272, 0.534, 0.131))
    );
}

void main() {
    float _ = u_time;
    vec4 color = texture(color_map_unit, tex_uv);
    color.rgb  = sepia(color.rgb);
    fragColor  = color;
}

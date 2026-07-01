#version 410

uniform sampler2D color_map_unit;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

void main() {
    float _ = u_time;
    vec4  color  = texture(color_map_unit, tex_uv);
    color.rgb    = floor(color.rgb * 5.0) / 5.0;
    fragColor    = color;
}

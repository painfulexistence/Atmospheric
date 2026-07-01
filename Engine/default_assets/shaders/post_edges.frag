#version 410

uniform sampler2D color_map_unit;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

void main() {
    float _ = u_time;
    vec2 ts = 1.0 / vec2(textureSize(color_map_unit, 0));
    vec4 h  = texture(color_map_unit, tex_uv + vec2(ts.x, 0.0)) -
              texture(color_map_unit, tex_uv - vec2(ts.x, 0.0));
    vec4 v  = texture(color_map_unit, tex_uv + vec2(0.0, ts.y)) -
              texture(color_map_unit, tex_uv - vec2(0.0, ts.y));
    fragColor = vec4(sqrt((h * h + v * v).rgb), 1.0);
}

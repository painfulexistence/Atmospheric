#version 410

uniform sampler2D color_map_unit;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

void main() {
    float _ = u_time;
    float u = tex_uv.x, v = tex_uv.y;
    float du = (u - 0.5) * (u - 0.5) * 0.01;
    float dv = (v - 0.5) * (v - 0.5) * 0.01;

    float r = texture(color_map_unit, vec2(u - 2.0*du, v + 4.0*dv)).r;
    float g = texture(color_map_unit, vec2(u + 1.0*du, v - 1.0*dv)).g;
    float b = texture(color_map_unit, vec2(u + 5.0*du, v - 3.0*dv)).b;

    fragColor = vec4(r, g, b, 1.0);
}

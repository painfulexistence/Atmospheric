#version 410

uniform sampler2D color_map_unit;
uniform float     exposure;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

const float gamma     = 2.2;
const float inv_gamma = 1.0 / gamma;

void main() {
    vec2 uv = (tex_uv - 0.5) * 2.0;
    vec2 offset = abs(uv.yx) * vec2(0.2, 0.25);
    uv += uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 col;
    col.r = texture(color_map_unit, uv + vec2(0.001, 0.0)).r;
    col.g = texture(color_map_unit, uv).g;
    col.b = texture(color_map_unit, uv - vec2(0.001, 0.0)).b;

    col += sin(uv.y * 800.0 + u_time * 10.0) * 0.04;

    col  = pow(col, vec3(gamma));
    col *= exposure * 0.66;
    col  = pow(col, vec3(inv_gamma));

    fragColor = vec4(col, 1.0);
}

#version 410

uniform sampler2D color_map_unit;
uniform float     exposure;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

const float gamma     = 2.2;
const float inv_gamma = 1.0 / gamma;

vec3 uncharted2(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

void main() {
    float _ = u_time;
    vec2  uv = tex_uv * 2.0 - 1.0;
    float vignette = 1.0 - dot(uv, uv) * 0.7;

    vec3 col = texture(color_map_unit, tex_uv).rgb;
    col  = pow(col, vec3(gamma));
    col *= exposure;
    col  = uncharted2(col);
    col *= vignette;
    col  = pow(col, vec3(inv_gamma));

    fragColor = vec4(col, 1.0);
}

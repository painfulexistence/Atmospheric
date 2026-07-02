#version 410

uniform sampler2D color_map_unit;
uniform float     exposure;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

const float gamma       = 2.2;
const float inv_gamma   = 1.0 / gamma;
const vec3  edge_color  = vec3(0.1);

float sobelColor(vec2 uv) {
    vec2 ts = 1.0 / vec2(textureSize(color_map_unit, 0));
    float tl = length(texture(color_map_unit, uv + vec2(-ts.x,  ts.y)).rgb);
    float t  = length(texture(color_map_unit, uv + vec2( 0.0,   ts.y)).rgb);
    float tr = length(texture(color_map_unit, uv + vec2( ts.x,  ts.y)).rgb);
    float l  = length(texture(color_map_unit, uv + vec2(-ts.x,  0.0 )).rgb);
    float r  = length(texture(color_map_unit, uv + vec2( ts.x,  0.0 )).rgb);
    float bl = length(texture(color_map_unit, uv + vec2(-ts.x, -ts.y)).rgb);
    float b  = length(texture(color_map_unit, uv + vec2( 0.0,  -ts.y)).rgb);
    float br = length(texture(color_map_unit, uv + vec2( ts.x, -ts.y)).rgb);
    float gx = -tl + tr - 2.0*l + 2.0*r - bl + br;
    float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
    return sqrt(gx*gx + gy*gy);
}

void main() {
    float _ = u_time;
    vec3 col = texture(color_map_unit, tex_uv).rgb;

    col  = pow(col, vec3(gamma));
    col *= exposure;

    float brightness   = length(col);
    float edge_strength = 1.0 - smoothstep(2.0, 4.0, brightness);
    float edge = smoothstep(0.1, 0.5, sobelColor(tex_uv));
    col = mix(col, edge_color, edge * edge_strength);

    col = pow(col, vec3(inv_gamma));
    fragColor = vec4(col, 1.0);
}

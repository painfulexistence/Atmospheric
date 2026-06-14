#version 410

uniform sampler2D color_map_unit;
uniform float exposure;
uniform bool  u_ca_enabled;
uniform float u_ca_strength;

in vec2 tex_uv;
out vec4 Color;

const float gamma = 2.2;

vec3 GetPixelCA(vec2 uv)
{
    float u = uv.x;
    float v = uv.y;
    float du = (u - 0.5) * (u - 0.5) * u_ca_strength;
    float dv = (v - 0.5) * (v - 0.5) * u_ca_strength;

    float r = texture(color_map_unit, vec2(u - 2.0 * du, v + 4.0 * dv)).r;
    float g = texture(color_map_unit, vec2(u + 1.0 * du, v - 1.0 * dv)).g;
    float b = texture(color_map_unit, vec2(u + 5.0 * du, v - 3.0 * dv)).b;
    return vec3(r, g, b);
}

vec3 CalculateToneMapping(vec3 hdrColor)
{
    return vec3(1.0) - exp(-hdrColor * exposure);
}

void main()
{
    vec3 hdrColor = u_ca_enabled
        ? GetPixelCA(tex_uv)
        : texture(color_map_unit, tex_uv).rgb;

    hdrColor = pow(hdrColor, vec3(gamma));
    vec3 result = CalculateToneMapping(hdrColor);
    result = pow(result, vec3(1.0 / gamma));

    Color = vec4(result, 1.0);
}

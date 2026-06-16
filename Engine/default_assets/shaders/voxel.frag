#version 410 core

in vec3 v_worldPos;
in vec3 v_normal;
flat in uint v_voxelId;
flat in uint v_faceId;

uniform vec3  u_lightDir;
uniform vec3  u_lightColor;
uniform vec3  u_ambientColor;
uniform vec3  u_fogColor;
uniform float u_fogDensity;
uniform vec3  u_cameraPos;

out vec4 fragColor;

// VX Palette 5 cosine palette: a + b * cos(2π * (c*t + d))
vec3 palette(float t) {
    vec3 a = vec3(0.746, 0.815, 0.846);
    vec3 b = vec3(0.195, 0.283, 0.187);
    vec3 c = vec3(1.093, 1.417, 1.405);
    vec3 d = vec3(5.435, 2.400, 5.741);
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    vec3 norm = normalize(v_normal);

    float diff = max(dot(norm, normalize(u_lightDir)), 0.0);

    vec3 baseColor = palette(float(v_voxelId) / 50.0);

    // Face shading levels matching VX: top=1.0, bottom=0.5, sides=0.9
    float faceShade = (v_faceId == 0u) ? 1.0 : (v_faceId == 1u ? 0.5 : 0.9);
    baseColor *= faceShade;

    vec3 ambient = u_ambientColor * baseColor;
    vec3 diffuse = diff * u_lightColor * baseColor;
    vec3 color   = ambient + diffuse;

    float dist      = length(v_worldPos - u_cameraPos);
    float fogFactor = clamp(exp(-u_fogDensity * dist), 0.0, 1.0);
    color = mix(u_fogColor, color, fogFactor);

    fragColor = vec4(color, 1.0);
}

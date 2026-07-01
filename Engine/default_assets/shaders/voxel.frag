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

uniform int   u_paletteIndex;  // 0-5, default 4 (VX Palette 5)

out vec4 fragColor;

// Cosine colour palette: a + b * cos(2π * (c*t + d))
// All 6 palettes ported from VX shaders/chunk.vert
vec3 palette(float t) {
    vec3 a, b, c, d;
    switch (u_paletteIndex) {
        case 0: // Palette 1 — warm pink/gold
            a = vec3(0.800, 0.150, 0.560); b = vec3(0.610, 0.300, 0.120);
            c = vec3(0.640, 0.100, 0.590); d = vec3(0.380, 0.860, 0.470); break;
        case 1: // Palette 2 — cool blue/purple
            a = vec3(0.288, 0.303, 0.466); b = vec3(0.806, 0.664, 0.998);
            c = vec3(1.253, 0.992, 1.569); d = vec3(3.379, 3.574, 3.026); break;
        case 2: // Palette 3 — earthy green
            a = vec3(0.420, 0.696, 0.625); b = vec3(0.791, 0.182, 0.271);
            c = vec3(0.368, 0.650, 0.103); d = vec3(0.913, 3.624, 0.320); break;
        case 3: // Palette 4 — forest
            a = vec3(0.427, 0.346, 0.372); b = vec3(0.288, 0.918, 0.336);
            c = vec3(0.635, 1.136, 0.404); d = vec3(1.893, 0.663, 1.910); break;
        case 5: // Palette 6 — vivid mint/coral
            a = vec3(0.686, 0.933, 0.933); b = vec3(0.957, 0.643, 0.957);
            c = vec3(0.867, 0.627, 0.867); d = vec3(1.961, 2.871, 1.702); break;
        default: // Palette 5 — soft cool blue-grey (VX default)
            a = vec3(0.746, 0.815, 0.846); b = vec3(0.195, 0.283, 0.187);
            c = vec3(1.093, 1.417, 1.405); d = vec3(5.435, 2.400, 5.741); break;
    }
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

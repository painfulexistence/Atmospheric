#version 410 core

in vec3 v_viewDir;

uniform vec3 u_skyColor;
uniform vec3 u_horizonColor;

// Equirectangular HDR environment. When u_useEnv == 1 the sky is sampled from
// u_envMap by view direction; otherwise the procedural gradient is used.
uniform sampler2D u_envMap;
uniform int u_useEnv;

out vec4 fragColor;

const vec2 INV_ATAN = vec2(0.1591549, 0.3183099);// (1/2π, 1/π)

// Direction -> equirectangular UV (LearnOpenGL's SampleSphericalMap).
vec2 dirToEquirectUV(vec3 dir) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
    return uv * INV_ATAN + 0.5;
}

void main() {
    vec3 dir = normalize(v_viewDir);
    vec3 col;
    if (u_useEnv == 1) {
        // HDR (linear); the tonemap post pass compresses it, like every other
        // color written into the HDR scene target.
        col = texture(u_envMap, dirToEquirectUV(dir)).rgb;
    } else {
        float t = clamp((dir.y + 1.0) * 0.5, 0.0, 1.0);
        col = mix(u_horizonColor, u_skyColor, t);
    }
    fragColor = vec4(col, 1.0);
}

#version 410 core

in vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texelSize;

out vec4 fragColor;

// 13-tap Kawase-style downsample used in VX / COD: MW
void main() {
    vec3 a = texture(u_src, v_uv + u_texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(u_src, v_uv + u_texelSize * vec2( 0.0, -1.0)).rgb;
    vec3 c = texture(u_src, v_uv + u_texelSize * vec2( 1.0, -1.0)).rgb;
    vec3 d = texture(u_src, v_uv + u_texelSize * vec2(-0.5, -0.5)).rgb;
    vec3 e = texture(u_src, v_uv + u_texelSize * vec2( 0.5, -0.5)).rgb;
    vec3 f = texture(u_src, v_uv + u_texelSize * vec2(-1.0,  0.0)).rgb;
    vec3 g = texture(u_src, v_uv).rgb;
    vec3 h = texture(u_src, v_uv + u_texelSize * vec2( 1.0,  0.0)).rgb;
    vec3 i = texture(u_src, v_uv + u_texelSize * vec2(-0.5,  0.5)).rgb;
    vec3 j = texture(u_src, v_uv + u_texelSize * vec2( 0.5,  0.5)).rgb;
    vec3 k = texture(u_src, v_uv + u_texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 l = texture(u_src, v_uv + u_texelSize * vec2( 0.0,  1.0)).rgb;
    vec3 m = texture(u_src, v_uv + u_texelSize * vec2( 1.0,  1.0)).rgb;

    vec3 result =
        g  * 0.125 +
        (b + f + h + l) * 0.0625 +
        (a + c + k + m) * 0.03125 +
        (d + e + i + j) * 0.125;

    fragColor = vec4(result, 1.0);
}

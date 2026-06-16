#version 410 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform vec3      u_lightDir;
uniform vec3      u_lightColor;
uniform vec3      u_cameraPos;
uniform vec3      u_fogColor;
uniform float     u_fogDensity;
uniform float     u_waterLine;
uniform sampler2D u_depthTexture;
uniform mat4      u_invProj;
uniform mat4      u_invView;

const vec3  DEEP_COLOR    = vec3(0.04, 0.11, 0.35);
const vec3  SHALLOW_COLOR = vec3(0.686, 0.933, 0.933);
const float BEER_COEF     = 0.095;

out vec4 fragColor;

vec3 reconstructWorldPos(vec2 screenUV, float depth) {
    vec4 clipPos = vec4(screenUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = u_invProj * clipPos;
    viewPos     /= viewPos.w;
    return (u_invView * viewPos).xyz;
}

void main() {
    vec3 norm     = normalize(v_normal);
    vec3 viewDir  = normalize(u_cameraPos - v_worldPos);
    vec3 lightDir = normalize(u_lightDir);
    vec3 halfDir  = normalize(lightDir + viewDir);

    // Underwater camera tint
    if (u_cameraPos.y < u_waterLine) {
        float sub = smoothstep(2.0, 32.0, u_waterLine - u_cameraPos.y);
        fragColor = vec4(mix(SHALLOW_COLOR, DEEP_COLOR, sub), 0.9);
        return;
    }

    // Beer-Lambert depth via screen-space depth reconstruction
    vec2  screenUV       = gl_FragCoord.xy / vec2(textureSize(u_depthTexture, 0));
    float rawDepth       = texture(u_depthTexture, screenUV).r;
    vec3  floorPos       = reconstructWorldPos(screenUV, rawDepth);
    float waterThickness = max(v_worldPos.y - floorPos.y, 0.0);
    float beerFactor     = max(1.0 - exp(-waterThickness * BEER_COEF), 0.0);
    vec3  col            = mix(SHALLOW_COLOR, DEEP_COLOR, beerFactor);

    float diff    = max(dot(norm, lightDir), 0.0);
    float spec    = pow(max(dot(norm, halfDir), 0.0), 128.0);
    float fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 5.0);

    col = mix(col, col * diff * u_lightColor + vec3(1.0) * spec * u_lightColor, 0.2);
    col = mix(col, vec3(1.0), fresnel * 0.5);

    float dist = length(v_worldPos - u_cameraPos);
    col = mix(u_fogColor, col, clamp(exp(-u_fogDensity * dist * dist), 0.0, 1.0));

    fragColor = vec4(col, smoothstep(0.1, 0.9, beerFactor + 0.3));
}

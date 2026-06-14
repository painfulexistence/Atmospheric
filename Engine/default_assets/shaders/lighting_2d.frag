// GL 4.1 fragment shader for LightingSystem2D fullscreen post-process.
// Samples the scene offscreen texture and applies ambient + up to 16 point lights.
// Ported from 2d-engine/src/fx/lighting.ts  LIGHTING_WGSL.
//
// For WebGL2 / Emscripten, replace the first two lines with:
//   #version 300 es
//   precision highp float;

#version 410

#define MAX_LIGHTS 16

struct PointLight {
    vec2  pos;
    vec3  color;
    float radius;
    float intensity;
};

uniform sampler2D u_scene;
uniform vec4      u_ambient;
uniform int       u_lightCount;
uniform PointLight u_lights[MAX_LIGHTS];
uniform vec2      u_screenSize;

in  vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 scene  = texture(u_scene, v_uv);
    vec2 pixPos = v_uv * u_screenSize;

    vec3 lightAccum = u_ambient.rgb * u_ambient.a;

    for (int i = 0; i < u_lightCount && i < MAX_LIGHTS; i++) {
        float d     = length(pixPos - u_lights[i].pos);
        float atten = max(0.0, 1.0 - d / u_lights[i].radius);
        lightAccum += u_lights[i].color * (u_lights[i].intensity * atten * atten);
    }

    fragColor = vec4(scene.rgb * min(lightAccum, vec3(1.0)), scene.a);
}

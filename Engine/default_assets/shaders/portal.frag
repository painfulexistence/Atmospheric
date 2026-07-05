#version 410 core

in vec3 v_worldPos;
in vec2 v_uv;
in vec3 v_normal;

// The recursion image this surface reveals (rendered by PortalPass from the
// virtual camera whose viewProj is u_portalViewProj). u_hasView == 0 when the
// portal is unlinked or the recursion floor was reached: paint the void fill.
uniform sampler2D u_portalTex;
uniform mat4      u_portalViewProj;
uniform vec3      u_rimColor;
uniform vec3      u_cameraPos;
uniform float     u_hasView;

out vec4 fragColor;

const vec3 VOID_COLOR = vec3(0.01, 0.01, 0.03);

void main() {
    // Concentric disc UVs: rim distance is 0 at the center, 1 at the edge.
    float r = length(v_uv - vec2(0.5)) * 2.0;

    // Only the front (+Z) side is a window; the back shows the void.
    float facing = dot(normalize(v_normal), normalize(u_cameraPos - v_worldPos));

    // Sample unconditionally, then select — implicit-LOD sampling inside a
    // varying-dependent branch has undefined derivatives in GLSL.
    vec4 clip = u_portalViewProj * vec4(v_worldPos, 1.0);
    vec2 uv   = clip.xy / max(abs(clip.w), 0.0001) * sign(clip.w) * 0.5 + 0.5;
    vec3 sampled = texture(u_portalTex, clamp(uv, vec2(0.001), vec2(0.999))).rgb;

    vec3 col = (u_hasView > 0.5 && facing > 0.0) ? sampled : VOID_COLOR;

    // HDR rim glow (feeds bloom), matching the game's colored portal edges.
    float rim = smoothstep(0.82, 0.95, r);
    col = mix(col, u_rimColor * 2.5, rim);

    fragColor = vec4(col, 1.0);
}

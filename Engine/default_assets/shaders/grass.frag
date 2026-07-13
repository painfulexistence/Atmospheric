#version 410

// Grass blade shading, tuned for the Ghost-of-Tsushima pampas look:
// a dark-root to golden-tip gradient (the root darkening doubles as baked
// ambient occlusion), soft wrap lighting so blades never go black against the
// sun, a translucency kick when backlit, and a subtle brightening where gust
// waves sweep the field. Fog matches terrain.frag so the grass sits in the
// same aerial perspective.

struct DirLight
{
    vec3 direction;
    vec3 diffuse;
};

uniform DirLight main_light;
uniform vec3 cam_pos;
uniform vec3 u_root_color;
uniform vec3 u_tip_color;
uniform vec3 fog_color;
uniform float fog_density;

in vec3 frag_pos;
in vec3 frag_normal;
in vec2 frag_uv;
in float frag_hue;
in float frag_gust;

layout(location = 0) out vec4 Color;

const float gamma = 2.2;

void main()
{
    float t = frag_uv.y;

    // Two-sided normal.
    vec3 N = normalize(frag_normal);
    if (!gl_FrontFacing) N = -N;

    // Root->tip gradient with per-blade variation (some blades greener/darker,
    // some brighter — breaks up the field into individual blades).
    vec3 albedo = mix(u_root_color, u_tip_color, t * t * 1.2);
    albedo *= 0.85 + 0.3 * frag_hue;
    albedo = pow(albedo, vec3(gamma));

    vec3 L = normalize(-main_light.direction);
    vec3 V = normalize(cam_pos - frag_pos);

    // Wrap diffuse: thin blades scatter light, so shift the lambert term up
    // instead of letting back faces go black.
    float ndl = clamp(dot(N, L) * 0.6 + 0.4, 0.0, 1.0);

    // Backlit translucency: looking toward the sun through the blades makes
    // the tips glow — the signature golden-hour grass shot.
    float backlit = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * t * 0.6;

    // Gust waves catch the light as they flatten the blades.
    float gustGlint = frag_gust * t * 0.15;

    vec3 lit = albedo * main_light.diffuse * (ndl + backlit + gustGlint);
    lit += albedo * 0.25 * (0.4 + 0.6 * t);// ambient, occluded toward the root

    // Aerial perspective, identical formula to terrain.frag.
    if (fog_density > 0.0) {
        float fogF = 1.0 - exp(-fog_density * distance(frag_pos, cam_pos));
        lit = mix(lit, fog_color, fogF);
    }

    Color = vec4(pow(lit, vec3(1.0 / gamma)), 1.0);
}

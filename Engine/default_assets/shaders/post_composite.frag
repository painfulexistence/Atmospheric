#version 410

// Single-pass post-process composite. Every effect is an independent toggle,
// applied in a fixed order:
//
//   VHS/CRT UV distortion -> per-channel fetch (chromatic aberration)
//   -> decode to linear HDR -> sobel -> tonemap -> edges -> color grading
//   -> posterize -> vignette -> VHS/CRT overlays -> gamma encode
//
// Geometry distortion must run before sampling; grading/posterize/vignette are
// display-domain stylizations so they run after tonemapping.
//
// VHS effect attribution/license: see ATTRIBUTION.txt in this directory.

uniform sampler2D color_map_unit;
uniform float exposure;
uniform float u_time;

uniform bool  u_ca_enabled;
uniform float u_ca_strength;
uniform bool  u_crt_enabled;
uniform bool  u_vhs_enabled;
uniform bool  u_grading_enabled;
uniform bool  u_posterize_enabled;
uniform bool  u_sobel_enabled;
uniform bool  u_edges_enabled;
uniform bool  u_vignette_enabled;

in  vec2 tex_uv;
out vec4 fragColor;

const float gamma     = 2.2;
const float inv_gamma = 1.0 / gamma;

// ─── VHS helpers ────────────────────────────────────────────────────────────

float rnd(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233 * cos(u_time)))) * 43758.5453);
}

float onOff(float a, float b, float c) {
    return step(c, sin(u_time + a * cos(u_time * b)));
}

vec2 vhsScreenDistort(vec2 uv) {
    uv -= 0.5;
    uv  = uv * 1.2 * (1.0/1.2 + 2.0*uv.x*uv.x*uv.y*uv.y);
    return uv + 0.5;
}

vec2 vhsTracking(vec2 uv) {
    vec2 look = uv;
    float window = 1.0 / (1.0 + 20.0*(look.y - mod(u_time/4.0,1.0))*(look.y - mod(u_time/4.0,1.0)));
    look.x += sin(look.y*10.0 + u_time)/50.0 * onOff(4.0,4.0,0.3) * (1.0+cos(u_time*80.0)) * window;
    float vShift = 0.4 * onOff(2.0,3.0,0.9) * (sin(u_time)*sin(u_time*20.0) + (0.5+0.1*sin(u_time*200.0)*cos(u_time)));
    look.y = mod(look.y + vShift, 1.0);
    return look;
}

// ─── Edge detection helpers ─────────────────────────────────────────────────

float sobelMagnitude(vec2 uv) {
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

vec3 edgeGradient(vec2 uv) {
    vec2 ts = 1.0 / vec2(textureSize(color_map_unit, 0));
    vec4 h  = texture(color_map_unit, uv + vec2(ts.x, 0.0)) -
              texture(color_map_unit, uv - vec2(ts.x, 0.0));
    vec4 v  = texture(color_map_unit, uv + vec2(0.0, ts.y)) -
              texture(color_map_unit, uv - vec2(0.0, ts.y));
    return sqrt((h * h + v * v).rgb);
}

// ─── Color helpers ──────────────────────────────────────────────────────────

vec3 sepia(vec3 c) {
    return vec3(
        dot(c, vec3(0.393, 0.769, 0.189)),
        dot(c, vec3(0.349, 0.686, 0.168)),
        dot(c, vec3(0.272, 0.534, 0.131))
    );
}

void main() {
    vec2 uv = tex_uv;

    // VHS: screen curvature + tracking wobble; vignette/stripe factors are
    // computed from the curved-but-untracked uv and applied post-tonemap.
    float vhsVignette = 1.0;
    float vhsStripe   = 1.0;
    if (u_vhs_enabled) {
        uv = vhsScreenDistort(uv);
        float vigAmt = 3.0 + 0.3*sin(u_time + 5.0*cos(u_time*5.0));
        vhsVignette  = (1.0 - vigAmt*(uv.y-0.5)*(uv.y-0.5)) *
                       (1.0 - vigAmt*(uv.x-0.5)*(uv.x-0.5));
        vhsStripe    = (12.0 + mod(uv.y*30.0 + u_time, 1.0)) / 13.0;
        uv = vhsTracking(uv);
    }

    // CRT: barrel distortion; samples pushed off-screen render black.
    bool offScreen = false;
    if (u_crt_enabled) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 offset = abs(c.yx) * vec2(0.2, 0.25);
        c += c * offset * offset;
        uv = c * 0.5 + 0.5;
        offScreen = uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0;
    }

    // Fetch. CA and the CRT phosphor offset both displace channels, so they
    // share one 3-tap path; otherwise a single tap.
    vec3 col;
    if (u_ca_enabled || u_crt_enabled) {
        vec2 rOff = vec2(0.0), gOff = vec2(0.0), bOff = vec2(0.0);
        if (u_ca_enabled) {
            float du = (uv.x - 0.5) * (uv.x - 0.5) * u_ca_strength;
            float dv = (uv.y - 0.5) * (uv.y - 0.5) * u_ca_strength;
            rOff += vec2(-2.0 * du,  4.0 * dv);
            gOff += vec2( 1.0 * du, -1.0 * dv);
            bOff += vec2( 5.0 * du, -3.0 * dv);
        }
        if (u_crt_enabled) {
            rOff += vec2( 0.001, 0.0);
            bOff += vec2(-0.001, 0.0);
        }
        col.r = texture(color_map_unit, uv + rOff).r;
        col.g = texture(color_map_unit, uv + gOff).g;
        col.b = texture(color_map_unit, uv + bOff).b;
    } else {
        col = texture(color_map_unit, uv).rgb;
    }

    col = pow(col, vec3(gamma));

    // Sobel runs in the HDR domain: bright pixels suppress the edge overlay.
    if (u_sobel_enabled) {
        float brightness    = length(col * exposure);
        float edge_strength = 1.0 - smoothstep(2.0, 4.0, brightness);
        float edge          = smoothstep(0.1, 0.5, sobelMagnitude(uv));
        col = mix(col, vec3(0.1), edge * edge_strength);
    }

    vec3 ldr = vec3(1.0) - exp(-col * exposure);

    // Edges replaces the image with the raw gradient magnitude (no tonemap),
    // but later stylize stages still apply on top of it.
    if (u_edges_enabled) ldr = edgeGradient(uv);

    if (u_grading_enabled)   ldr = sepia(ldr);
    if (u_posterize_enabled) ldr = floor(ldr * 5.0) / 5.0;
    if (u_vignette_enabled) {
        vec2 c = tex_uv * 2.0 - 1.0;
        ldr *= 1.0 - dot(c, c) * 0.7;
    }
    ldr *= vhsVignette * vhsStripe;
    if (u_crt_enabled) ldr += sin(uv.y*800.0 + u_time*10.0) * 0.04;

    ldr = pow(max(ldr, vec3(0.0)), vec3(inv_gamma));
    fragColor = offScreen ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(ldr, 1.0);
}

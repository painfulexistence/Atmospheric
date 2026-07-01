#version 410

/* License CC BY-NC-SA 4.0 Deed */
/* https://creativecommons.org/licenses/by-nc-sa/4.0/ */
/* Fork of Ryk's VCR distortion shader */
/* https://www.shadertoy.com/view/ldjGzV */

uniform sampler2D color_map_unit;
uniform float     exposure;
uniform float     u_time;

in  vec2 tex_uv;
out vec4 fragColor;

const float gamma     = 2.2;
const float inv_gamma = 1.0 / gamma;

vec3 uncharted2(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float rnd(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233 * cos(u_time)))) * 43758.5453);
}

float noise(vec2 st) {
    vec2 i = floor(st), f = fract(st);
    float a = rnd(i), b = rnd(i + vec2(1,0)), c = rnd(i + vec2(0,1)), d = rnd(i + vec2(1,1));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a,b,u.x) + (c-a)*u.y*(1.0-u.x) + (d-b)*u.x*u.y;
}

float onOff(float a, float b, float c) {
    return step(c, sin(u_time + a * cos(u_time * b)));
}

float ramp(float y, float start, float end) {
    float inside = step(start,y) - step(end,y);
    return (1.0 - (y-start)/(end-start)*inside) * inside;
}

vec2 screenDistort(vec2 uv) {
    uv -= 0.5;
    uv  = uv * 1.2 * (1.0/1.2 + 2.0*uv.x*uv.x*uv.y*uv.y);
    return uv + 0.5;
}

void main() {
    vec2 uv   = screenDistort(tex_uv);
    vec2 look = uv;

    float window = 1.0 / (1.0 + 20.0*(look.y - mod(u_time/4.0,1.0))*(look.y - mod(u_time/4.0,1.0)));
    look.x += sin(look.y*10.0 + u_time)/50.0 * onOff(4.0,4.0,0.3) * (1.0+cos(u_time*80.0)) * window;
    float vShift = 0.4 * onOff(2.0,3.0,0.9) * (sin(u_time)*sin(u_time*20.0) + (0.5+0.1*sin(u_time*200.0)*cos(u_time)));
    look.y = mod(look.y + vShift, 1.0);

    float vigAmt  = 3.0 + 0.3*sin(u_time + 5.0*cos(u_time*5.0));
    float vignette = (1.0 - vigAmt*(uv.y-0.5)*(uv.y-0.5)) *
                     (1.0 - vigAmt*(uv.x-0.5)*(uv.x-0.5));

    vec3 col = texture(color_map_unit, look).rgb;
    col  = pow(col, vec3(gamma));
    col *= exposure;
    col  = uncharted2(col);
    col *= vignette;
    col *= (12.0 + mod(uv.y*30.0 + u_time, 1.0)) / 13.0;
    col  = pow(col, vec3(inv_gamma));

    fragColor = vec4(col, 1.0);
}

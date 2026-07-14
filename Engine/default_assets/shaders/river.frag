#version 410

// River water surface. Procedural flowing water without a depth/refraction
// buffer (works in the plain forward pass, no G-buffer dependency):
//   - two normal-wave bands scrolling DOWNSTREAM (along UV.y) at different
//     speeds/scales give the moving ripple;
//   - fresnel blends deep channel colour -> brighter shallow/edge colour and
//     drives the surface reflectivity;
//   - a sharp sun glint off the perturbed normal reads as sparkle on the flow;
//   - foam streaks near the banks (UV.x -> 0/1) scroll with the current;
//   - the same aerial-perspective fog as the terrain so far rivers recede.
// Alpha-blended over the terrain (RiverMaterial sets Transparent + cull none).

uniform vec3 cam_pos;
uniform float u_time;
uniform float u_flow_speed;
uniform float u_ripple;
uniform float u_glint;
uniform float u_alpha;
uniform vec3 u_shallow_color;
uniform vec3 u_deep_color;
uniform vec3 fog_color;
uniform float fog_density;

struct DirLight { vec3 direction; vec3 diffuse; };
uniform DirLight main_light;

in vec3 frag_pos;
in vec2 frag_uv;
in vec3 flow_dir;
in vec3 side_dir;

layout(location = 0) out vec4 Color;

const float gamma = 2.2;

// Cheap value-noise gradient for the ripple normal.
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p){
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = hash(i), b = hash(i + vec2(1,0)), c = hash(i + vec2(0,1)), d = hash(i + vec2(1,1));
	return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
}

void main(void)
{
	// Scroll two noise bands downstream (+V) at different rates; their gradient
	// perturbs the up-normal in the flow/side tangent frame.
	float t = u_time * u_flow_speed;
	vec2 uvA = vec2(frag_uv.x * 3.0, frag_uv.y * 1.2 - t);
	vec2 uvB = vec2(frag_uv.x * 7.0 - 0.3, frag_uv.y * 2.7 - t * 1.7);
	float e = 0.05;
	float nA = vnoise(uvA), nB = vnoise(uvB);
	float gx = (vnoise(uvA + vec2(e,0)) - nA) + 0.5 * (vnoise(uvB + vec2(e,0)) - nB);
	float gy = (vnoise(uvA + vec2(0,e)) - nA) + 0.5 * (vnoise(uvB + vec2(0,e)) - nB);

	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 N = normalize(up + u_ripple * (side_dir * gx + flow_dir * gy));

	vec3 V = normalize(cam_pos - frag_pos);
	float fres = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 3.0);
	fres = 0.15 + 0.85 * fres;// never fully non-reflective at grazing 0

	// Deep centre -> shallow banks (UV.x is 0..1 across the channel).
	float edge = abs(frag_uv.x - 0.5) * 2.0;// 0 centre, 1 bank
	vec3 body = mix(u_deep_color, u_shallow_color, edge * edge);

	// Sky-ish reflection tint stands in for a real reflection probe.
	vec3 skyTint = mix(u_shallow_color, vec3(0.75, 0.85, 0.95), 0.5);
	vec3 col = mix(body, skyTint, fres * 0.6);

	// Sun glint off the rippled normal.
	vec3 L = normalize(-main_light.direction);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), 120.0) * u_glint;
	col += main_light.diffuse * spec;

	// Foam streaks hugging the banks, scrolling downstream.
	float bankMask = smoothstep(0.72, 0.98, edge);
	float foam = smoothstep(0.55, 0.85, vnoise(vec2(frag_uv.x * 9.0, frag_uv.y * 3.0 - t * 1.3)));
	col = mix(col, vec3(0.9, 0.95, 0.98), bankMask * foam * 0.6);

	float alpha = clamp(u_alpha + fres * 0.15 + bankMask * foam * 0.3, 0.0, 1.0);

	// Aerial perspective, matching terrain.frag.
	if (fog_density > 0.0) {
		float fogF = 1.0 - exp(-fog_density * distance(frag_pos, cam_pos));
		col = mix(col, fog_color, fogF);
	}

	Color = vec4(pow(col, vec3(1.0 / gamma)), alpha);
}

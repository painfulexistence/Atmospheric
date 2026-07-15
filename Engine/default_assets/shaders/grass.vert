#version 410

// Instanced grass blades (MeshType::GRASS). One canonical 9-vertex blade is
// drawn per instance; the per-blade transform (root/facing/length/lean) and
// variation live in the instance attributes, so a whole cell is one
// glDrawArraysInstanced call with ~32 bytes/blade instead of baked geometry.
//
// The shader turns the flat canonical blade into a curved, wind-swayed
// world-space blade: two sine bands (a slow travelling gust + a faster
// per-blade flutter) push the tip downwind with a t^2 weight so roots stay
// planted (Ghost-of-Tsushima style). Blades shrink to the ground near the
// ring edge so the streamed grass boundary is invisible.

uniform mat4 ProjectionView;
uniform mat4 World;
uniform vec3 cam_pos;
uniform float u_time;
uniform vec2 u_wind_dir;
uniform float u_wind_strength;
uniform float u_wind_speed;
uniform float u_fade_start;
uniform float u_fade_end;

layout(location = 0) in vec2 aBlade;       // x = side [-1,1], y = t [0,1]
layout(location = 5) in vec4 iRootFacing;  // xyz = root (cell-local), w = facing angle
layout(location = 6) in vec4 iShape;       // x = length, y = lean, z = phase, w = hue

out vec3 frag_pos;
out vec3 frag_normal;
out vec2 frag_uv;    // (side, t)
out float frag_hue;
out float frag_gust;

void main(void)
{
	float side = aBlade.x;
	float t = aBlade.y;
	vec3 root = iRootFacing.xyz;
	float bladeLen = iShape.x;
	float lean = iShape.y;
	float phase = iShape.z;

	float ca = cos(iRootFacing.w), sa = sin(iRootFacing.w);
	vec3 facing = vec3(ca, 0.0, sa);
	vec3 sideDir = vec3(-sa, 0.0, ca);

	vec3 rootWorld = (World * vec4(root, 1.0)).xyz;
	vec2 windDir = normalize(u_wind_dir);

	// Slow gust wave travelling downwind + faster per-blade flutter, plus a
	// constant downwind bias so the field leans "away from the wind".
	float gust = sin(dot(rootWorld.xz, windDir) * 0.10 - u_time * u_wind_speed);
	float flutter = sin(dot(rootWorld.xz, vec2(0.83, -0.55)) * 0.9 - u_time * (u_wind_speed * 2.7) + phase);
	float sway = (0.45 + 0.40 * gust + 0.15 * flutter) * u_wind_strength;

	// Rest-pose curve: static lean bends the blade forward along t^2. Wind adds
	// on top of the lean, both t^2-weighted so the root stays planted.
	float bendAmt = (lean + sway) * t * t;
	vec3 center = root
		+ facing * (bendAmt * bladeLen)
		+ vec3(0.0, t * bladeLen * (1.0 - 0.2 * bendAmt), 0.0);

	// Width tapers root->tip; the tip vertex has side=0 so it comes to a point.
	float halfWidth = bladeLen * 0.03 * (1.0 - 0.85 * t);
	vec3 pos = center + sideDir * (side * halfWidth);

	// Ring fade: collapse the blade toward its root approaching the edge.
	float dist = distance(cam_pos.xz, rootWorld.xz);
	float keep = 1.0 - smoothstep(u_fade_start, u_fade_end, dist);
	pos = mix(root, pos, keep);

	vec4 worldPos = World * vec4(pos, 1.0);
	frag_pos = worldPos.xyz;
	// Blade normal faces the camera side of the blade plane (billboard-ish),
	// so both faces catch light regardless of facing.
	frag_normal = mat3(World) * facing;
	frag_uv = vec2(side, t);
	frag_hue = iShape.w;
	frag_gust = clamp(0.5 + 0.5 * gust, 0.0, 1.0);
	gl_Position = ProjectionView * worldPos;
}

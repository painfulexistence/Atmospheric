#version 410

// Streamed grass blades (MeshType::GRASS). Each blade is baked into the cell
// mesh in its rest pose (static lean included); this shader adds the wind:
// two sine bands (a slow travelling gust plus a faster flutter) push the blade
// tip downwind with a t^2 weight so roots stay planted, Ghost-of-Tsushima
// style. Blades shrink to the ground near the ring edge so the streamed grass
// boundary is invisible.
//
// Vertex attribute encoding (see TerrainStreamer's grass cell builder):
//   position  - rest-pose vertex position, cell-local
//   uv        - (side in [-1,1], t: 0 at the root, 1 at the tip)
//   normal    - blade facing normal (horizontal)
//   tangent   - blade root position, cell-local
//   bitangent - (phase, blade length, color variation)

uniform mat4 ProjectionView;
uniform mat4 World;
uniform vec3 cam_pos;
uniform float u_time;
uniform vec2 u_wind_dir;
uniform float u_wind_strength;
uniform float u_wind_speed;
uniform float u_fade_start;
uniform float u_fade_end;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

out vec3 frag_pos;
out vec3 frag_normal;
out vec2 frag_uv;    // (side, t)
out float frag_hue;  // per-blade color variation
out float frag_gust; // gust intensity, brightens wind waves sweeping the field

void main(void)
{
	float t = uv.y;
	float phase = bitangent.x;
	float bladeLen = bitangent.y;

	vec3 rootWorld = (World * vec4(tangent, 1.0)).xyz;
	vec2 windDir = normalize(u_wind_dir);

	// Slow gust wave travelling downwind + faster per-blade flutter. The
	// constant bias keeps the field leaning downwind instead of oscillating
	// around vertical, which is what sells the "wind from over there" read.
	float gust = sin(dot(rootWorld.xz, windDir) * 0.10 - u_time * u_wind_speed);
	float flutter = sin(dot(rootWorld.xz, vec2(0.83, -0.55)) * 0.9 - u_time * (u_wind_speed * 2.7) + phase);
	float sway = (0.45 + 0.40 * gust + 0.15 * flutter) * u_wind_strength;

	// t^2 weighting: roots planted, tips travel. The small vertical drop keeps
	// the blade length roughly constant as it bends (arc, not shear).
	float bend = sway * t * t * bladeLen;
	vec3 pos = position;
	pos.xz += windDir * bend;
	pos.y -= bend * bend * (0.5 / max(bladeLen, 0.01));

	// Ring fade: collapse the blade toward its root as it approaches the edge
	// of the streamed grass radius.
	float dist = distance(cam_pos.xz, rootWorld.xz);
	float keep = 1.0 - smoothstep(u_fade_start, u_fade_end, dist);
	pos = mix(tangent, pos, keep);

	vec4 worldPos = World * vec4(pos, 1.0);
	frag_pos = worldPos.xyz;
	frag_normal = mat3(World) * normal;
	frag_uv = uv;
	frag_hue = bitangent.z;
	frag_gust = clamp(0.5 + 0.5 * gust, 0.0, 1.0);
	gl_Position = ProjectionView * worldPos;
}

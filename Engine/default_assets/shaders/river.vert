#version 410

// Draped river ribbon (MeshType::RIVER). The mesh is already built to hug the
// terrain (BuildRiverMesh samples the height source per vertex), so the vertex
// stage only transforms and forwards the flow frame; all the water motion is
// in the fragment shader. A tiny vertical bob gives the surface some life.

uniform mat4 ProjectionView;
uniform mat4 World;
uniform float u_time;
uniform float u_flow_speed;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;       // x = bank (0..1), y = along-flow (metres/uvMetresPerV)
layout(location = 2) in vec3 aNormal;   // up
layout(location = 3) in vec3 aTangent;  // downstream (flow direction)
layout(location = 4) in vec3 aBitangent;// across the channel

out vec3 frag_pos;
out vec2 frag_uv;
out vec3 flow_dir;   // world-space downstream direction
out vec3 side_dir;   // world-space across direction

void main(void)
{
	vec4 world = World * vec4(aPos, 1.0);

	// Subtle surface bob: two crossing waves along/across the channel so the
	// plane isn't glassy-flat. Amplitude is small (a few cm) — the ribbon must
	// stay glued to the bed it was draped onto.
	float bob = sin(aUV.y * 3.1 - u_time * u_flow_speed * 2.0) * 0.03
	          + sin(aUV.x * 6.0 + u_time * 0.7) * 0.02;
	world.y += bob;

	frag_pos = world.xyz;
	frag_uv = aUV;
	flow_dir = normalize(mat3(World) * aTangent);
	side_dir = normalize(mat3(World) * aBitangent);
	gl_Position = ProjectionView * world;
}

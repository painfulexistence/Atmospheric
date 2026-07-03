#version 410

uniform mat4 ProjectionView;
uniform mat4 World;
uniform float height_scale;
uniform sampler2D height_map_unit;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out vec2 frag_uv;
out vec3 frag_pos;
out float height;

void main(void)
{
	height = textureLod(height_map_unit, uv, 0.0).r;
	vec3 pos = position;
	pos.y += height * height_scale;

	vec4 world_pos = World * vec4(pos, 1.0);
	frag_uv = uv;
	frag_pos = world_pos.xyz;
	gl_Position = ProjectionView * world_pos;
}

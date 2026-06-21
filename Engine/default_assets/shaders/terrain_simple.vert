#version 410

uniform mat4 ProjectionView;
uniform mat4 World;
uniform float height_scale;
uniform sampler2D height_map_unit;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out float height;

void main(void)
{
	height = texture(height_map_unit, uv).r;
	vec3 pos = position;
	pos.y += height * height_scale;
	gl_Position = ProjectionView * World * vec4(pos, 1.0);
}

#version 410

uniform mat4 ProjectionView;
uniform mat4 World;
uniform float height_scale;
uniform sampler2D height_map_unit;

layout(quads, fractional_odd_spacing, ccw) in;

layout(location = 0) in vec2 tese_uv[];

out float height;

void main()
{
	vec2 uv1 = mix(tese_uv[0], tese_uv[1], gl_TessCoord.x);
	vec2 uv2 = mix(tese_uv[3], tese_uv[2], gl_TessCoord.x);
	vec2 tex_uv = mix(uv1, uv2, gl_TessCoord.y);

	vec4 pos1 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
	vec4 pos2 = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, gl_TessCoord.x);
	vec4 pos = mix(pos1, pos2, gl_TessCoord.y);

	height = textureLod(height_map_unit, tex_uv, 1.0).r;
	pos.y += height * height_scale;

	gl_Position = ProjectionView * World * pos;
}
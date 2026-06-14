#version 410

uniform mat4 ProjectionView;
uniform mat4 World;
uniform vec3 cam_pos;
uniform sampler2D height_map_unit;
uniform float tessellation_factor;

layout(vertices = 4) out;

layout(location = 0) in vec2 tesc_uv[];

out vec2 tese_uv[];


float cameraDistanceTessFactor(vec4 p0, vec4 p1)
{
	vec3 mid_point = (p0.xyz + p1.xyz) / 2.0;
	return clamp(30.0 / distance(cam_pos, mid_point) * tessellation_factor, 1.0, 32.0);
}

void main()
{
	if (gl_InvocationID == 0)
	{
		if (tessellation_factor > 0.0)
		{
			gl_TessLevelOuter[0] = cameraDistanceTessFactor(gl_in[0].gl_Position, gl_in[3].gl_Position);
			gl_TessLevelOuter[1] = cameraDistanceTessFactor(gl_in[1].gl_Position, gl_in[0].gl_Position);
			gl_TessLevelOuter[2] = cameraDistanceTessFactor(gl_in[2].gl_Position, gl_in[1].gl_Position);
			gl_TessLevelOuter[3] = cameraDistanceTessFactor(gl_in[3].gl_Position, gl_in[2].gl_Position);
			gl_TessLevelInner[0] = mix(gl_TessLevelOuter[0], gl_TessLevelOuter[3], 0.5);
			gl_TessLevelInner[1] = mix(gl_TessLevelOuter[2], gl_TessLevelOuter[1], 0.5);
		}
		else
		{
			gl_TessLevelInner[0] = 1.0;
			gl_TessLevelInner[1] = 1.0;
			gl_TessLevelOuter[0] = 1.0;
			gl_TessLevelOuter[1] = 1.0;
			gl_TessLevelOuter[2] = 1.0;
			gl_TessLevelOuter[3] = 1.0;
		}

	}

	gl_out[gl_InvocationID].gl_Position =  gl_in[gl_InvocationID].gl_Position;
	tese_uv[gl_InvocationID] = tesc_uv[gl_InvocationID];
}

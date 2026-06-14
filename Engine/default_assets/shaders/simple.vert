#version 410

uniform mat4 ProjectionView;
uniform mat4 World;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out vec2 frag_uv;

void main(void)
{
    frag_uv = uv;
    gl_Position = ProjectionView * World * vec4(position, 1.0);
}

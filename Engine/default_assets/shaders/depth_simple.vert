#version 410

uniform mat4 ProjectionView;

layout(location = 0) in vec3 position;
layout(location = 5) in mat4 World;

void main()
{
    gl_Position = ProjectionView * World * vec4(position, 1.0);
}
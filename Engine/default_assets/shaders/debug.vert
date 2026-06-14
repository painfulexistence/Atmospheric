#version 410

uniform mat4 ProjectionView;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

out vec3 fragColor;

void main()
{
    fragColor = color;
    gl_Position = ProjectionView * vec4(position, 1.0);
}
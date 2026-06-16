#version 410 core

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_uv;

// Per-instance attributes
layout (location = 2) in vec3 instance_position;
layout (location = 3) in vec4 instance_color;
layout (location = 4) in float instance_size;

uniform mat4 ProjectionView;
uniform vec3 cam_pos;

out vec4 frag_color;
out vec2 frag_uv;

void main() {
    // Billboard the quad to face the camera
    vec3 cam_right = vec3(1, 0, 0); // Simplified billboard
    vec3 cam_up = vec3(0, 1, 0);

    vec3 quad_pos = instance_position + (cam_right * in_position.x * instance_size) + (cam_up * in_position.y * instance_size);

    gl_Position = ProjectionView * vec4(quad_pos, 1.0);
    frag_color = instance_color;
    frag_uv = in_uv;
}

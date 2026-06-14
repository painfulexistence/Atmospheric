#version 410

uniform mat4 Projection;

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 uv;
layout(location = 3) in float texIndex;
layout(location = 4) in float entityID;

out vec4 fragColor;
out vec2 texUV;
flat out float tid;
flat out float eid;

void main() {
    fragColor = color;
    texUV = uv;
    tid = texIndex;
    eid = entityID;
    gl_Position = Projection * vec4(position, 1.0);
}
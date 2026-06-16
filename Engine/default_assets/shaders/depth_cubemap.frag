#version 410

uniform vec3 LightPosition;

in vec3 frag_pos;

void main()
{    
    gl_FragDepth = length(frag_pos - LightPosition) / 400.0f;
}
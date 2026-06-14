#version 410 core

in vec4 frag_color;
in vec2 frag_uv;

out vec4 out_color;

void main() {
    float alpha = 1.0 - length(frag_uv - vec2(0.5));
    out_color = frag_color * alpha;
}

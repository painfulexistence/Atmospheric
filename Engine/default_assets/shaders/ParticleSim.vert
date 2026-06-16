#version 410 core

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_velocity;
layout (location = 2) in vec4 in_color;
layout (location = 3) in float in_life;
layout (location = 4) in float in_size;

// The output block MUST match the input attributes for transform feedback
// The names MUST match the names given to the pipeline create info
out vec3 out_position;
out vec3 out_velocity;
out vec4 out_color;
out float out_life;
out float out_size;
out float out_pad0;
out float out_pad1;

uniform vec3 attractorPos;
uniform float deltaTime;

// Simple pseudo-random number generator
float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    float life = in_life - deltaTime;

    if (life <= 0.0) {
        // Respawn particle at origin (or a spawn point)
        float r = sqrt(rand(in_position.xy + deltaTime)) * 0.1;
        float theta = rand(in_position.yx + deltaTime) * 2.0 * 3.14159;
        
        // Emitter's position could be passed as a uniform
        out_position = vec3(r * cos(theta), r * sin(theta), 0.0);
        out_velocity = normalize(vec3(-out_position.y, out_position.x, 0.0)) * 0.5;
        out_color = vec4(rand(out_position.xy), rand(out_position.yz), rand(out_position.zx), 1.0);
        out_life = rand(in_position.xy) * 5.0 + 2.0; // New lifetime
        out_size = rand(in_position.yx) * 0.05 + 0.05;

    } else {
        // Update existing particle
        vec3 force = normalize(attractorPos - in_position) * 0.5;
        vec3 new_velocity = in_velocity + force * deltaTime;
        
        out_position = in_position + new_velocity * deltaTime;
        out_velocity = new_velocity;
        out_color = in_color;
        out_life = life;
        out_size = in_size;
    }

    // Must write to all outputs to avoid undefined behavior
    out_pad0 = 0.0;
    out_pad1 = 0.0;
}
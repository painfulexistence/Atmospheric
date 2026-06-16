#version 410

uniform sampler2D height_map_unit;

layout(location = 0) in float height;

out vec4 Color;

vec3 palette(float t) {
  vec3 a = vec3(0.80, 0.15, 0.56);
  vec3 b = vec3(0.61, 0.30, 0.12);
  vec3 c = vec3(0.64, 0.10, 0.59);
  vec3 d = vec3(0.38, 0.86, 0.47);
  return a + b * cos(6.28318 * (c * t + d));
}

void main()
{
	vec3 color = palette(height);
	Color = vec4(color, 1.0);
}

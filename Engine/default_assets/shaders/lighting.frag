#version 330 core

struct DirectionalLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float intensity;
};

struct PointLight {
    vec3 position;
    vec3 attenuation;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float intensity;
};

layout(location = 0) out vec4 FragColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gMaterial;
uniform DirectionalLight mainLight;
uniform PointLight pointLights[16];
uniform int pointLightCount;
uniform vec3 cam_pos;

in vec2 TexCoords;


const float PI = 3.1415927;
const float gamma = 2.2;
const float inv_gamma = 1.0 / gamma;

float distributionGGX(float nh, float r) {
    float a = r*r;
    float a2 = a*a;
    float nh2 = nh*nh;

    float nom = a2;
    float denom = (nh2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float geometrySchlickGGX(float nx, float r) {
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    float nom   = nx;
    float denom = nx * (1.0 - k) + k;
    return nom / denom;
}

float geometrySmith(float nv, float nl, float r) {
    float ggx2 = geometrySchlickGGX(nv, r);
    float ggx1 = geometrySchlickGGX(nl, r);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float vh, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - vh, 0.0, 1.0), 5.0);
}

vec3 cookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    float nh = max(dot(N, H), 0.0);
    float nv = max(dot(N, V), 0.0);
    float nl = max(dot(N, L), 0.0);
    float vh = max(dot(V, H), 0.0);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);

    float NDF = distributionGGX(nh, roughness);
    float G = geometrySmith(nv, nl, roughness);
    vec3 F = fresnelSchlick(vh, F0);

    vec3 specular = NDF * G * F / (4.0 * nv * nl + 0.0001);

    vec3 ks = F;
    vec3 kd = vec3(1.0) - ks;
    kd *= 1.0 - metallic;
    vec3 diffuse = kd * albedo.rgb / PI;

    return kd * albedo.rgb / PI + specular;
}

void main() {
    vec3 normal = texture(gNormal, TexCoords).rgb;
    vec3 frag_pos = texture(gPosition, TexCoords).rgb;
    vec4 albedo = texture(gAlbedo, TexCoords);
    vec3 material = texture(gMaterial, TexCoords).rgb;

    float roughness = material.r;
    float metallic = material.g;
    float ao = material.b;

    vec3 N = normalize(normal);
    vec3 V = normalize(cam_pos - frag_pos);

    vec3 Lo = vec3(0.0);

    vec3 L = normalize(-mainLight.direction);
    float nl = max(dot(N, L), 0.0);
    Lo += cookTorranceBRDF(N, V, L, albedo.rgb, roughness, metallic) * mainLight.diffuse * mainLight.intensity * nl;

    for (int i = 0; i < pointLightCount; ++i) {
        vec3 L = normalize(pointLights[i].position - frag_pos);
        float distance = length(pointLights[i].position - frag_pos);
        float attenuation = 1.0 / (pointLights[i].attenuation.x + pointLights[i].attenuation.y * distance + pointLights[i].attenuation.z * distance * distance);
        float nl = max(dot(N, L), 0.0);
        Lo += cookTorranceBRDF(N, V, L, albedo.rgb, roughness, metallic) * pointLights[i].diffuse * pointLights[i].intensity * attenuation * nl;
    }

    vec3 ambient = mainLight.ambient * albedo.rgb * ao;
    vec3 color = ambient + Lo;

    // Gamma correction
    color = pow(color, vec3(inv_gamma));

    FragColor = vec4(color, albedo.a);
}
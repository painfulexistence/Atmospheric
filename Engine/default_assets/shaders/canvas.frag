#version 410

in vec4 fragColor;
in vec2 texUV;
flat in float tid;
flat in float eid;

out vec4 Color;

uniform sampler2D Textures[16];

void main() {
    // TODO: use GL_TEXTURE_ARRAY
    // vec4 texColor = texture(textureArray, vec3(texUV, tid));
    vec4 texColor = fragColor;
    int index = int(tid + 0.5);
    switch(index) { // Using switch-case coz non-const indexing into uniform array is not supported by OpenGL spec (see https://stackoverflow.com/questions/57854484/passing-unsigned-int-input-attribute-to-vertex-shader)
    case 0: texColor *= texture(Textures[0], texUV); break;
    case 1: texColor *= texture(Textures[1], texUV); break;
    case 2: texColor *= texture(Textures[2], texUV); break;
    case 3: texColor *= texture(Textures[3], texUV); break;
    case 4: texColor *= texture(Textures[4], texUV); break;
    case 5: texColor *= texture(Textures[5], texUV); break;
    case 6: texColor *= texture(Textures[6], texUV); break;
    case 7: texColor *= texture(Textures[7], texUV); break;
    case 8: texColor *= texture(Textures[8], texUV); break;
    case 9: texColor *= texture(Textures[9], texUV); break;
    case 10: texColor *= texture(Textures[10], texUV); break;
    case 11: texColor *= texture(Textures[11], texUV); break;
    case 12: texColor *= texture(Textures[12], texUV); break;
    case 13: texColor *= texture(Textures[13], texUV); break;
    case 14: texColor *= texture(Textures[14], texUV); break;
    case 15: texColor *= texture(Textures[15], texUV); break;
    }
    Color = texColor;
}
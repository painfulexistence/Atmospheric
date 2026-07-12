#version 410

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Animation Texture (VAT) displacement.
//
// A VAT bakes a mesh's per-vertex animation into textures: one texel per vertex
// (column, addressed by gl_VertexID) per frame (row). At runtime the vertex
// stage looks up the vertex's animated object-space position (and normal) for
// the current playhead and uses it in place of the static attributes. This is
// the same runtime path a Houdini VAT ROP export drives — the only difference
// is where the texture came from (offline bake vs. the demo's runtime baker).
//
// No compute, no CPU skinning: the whole animation is a texture fetch, so it is
// cheap enough for many instances and runs on every GL/GLES/WebGL2 target.
// ─────────────────────────────────────────────────────────────────────────────

uniform mat4 ProjectionView;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;
layout(location = 5) in mat4 World;

uniform sampler2D vat_position_map;// RGB = object-space position per (vertex, frame)
uniform sampler2D vat_normal_map;  // RGB = object-space normal   per (vertex, frame)
uniform int   vat_vert_count;      // texture width  (vertices)
uniform int   vat_frame_count;     // texture height (frames)
uniform float vat_time;            // normalized playhead in [0, 1]
uniform int   vat_enabled;         // 0 = fall back to static attributes

// Position/normal remap for quantized (8/16-bit) VAT exports: the sampled texel
// (in [0,1]) is expanded into [vat_bounds_min, vat_bounds_max] and the normal
// from [0,1] into [-1,1]. Float (RGB32F) bakes store raw values and set
// vat_remap = 0.
uniform int  vat_remap;
uniform vec3 vat_bounds_min;
uniform vec3 vat_bounds_max;

out vec3 frag_pos;
out vec2 tex_uv;
out mat3 TBN;

void main() {
    vec3 localPos = position;
    vec3 localNorm = normal;

    if (vat_enabled == 1 && vat_vert_count > 0 && vat_frame_count > 0) {
        float u = (float(gl_VertexID) + 0.5) / float(vat_vert_count);

        // Blend between the two frames straddling the playhead so playback is
        // smooth even though the textures use NEAREST filtering (linear float
        // texture filtering is not guaranteed on GLES/WebGL2).
        float ft = vat_time * float(max(vat_frame_count - 1, 0));
        float f0 = floor(ft);
        float f1 = min(f0 + 1.0, float(vat_frame_count - 1));
        float fr = ft - f0;
        float v0 = (f0 + 0.5) / float(vat_frame_count);
        float v1 = (f1 + 0.5) / float(vat_frame_count);

        vec3 p0 = textureLod(vat_position_map, vec2(u, v0), 0.0).rgb;
        vec3 p1 = textureLod(vat_position_map, vec2(u, v1), 0.0).rgb;
        vec3 n0 = textureLod(vat_normal_map, vec2(u, v0), 0.0).rgb;
        vec3 n1 = textureLod(vat_normal_map, vec2(u, v1), 0.0).rgb;

        if (vat_remap == 1) {
            p0 = mix(vat_bounds_min, vat_bounds_max, p0);
            p1 = mix(vat_bounds_min, vat_bounds_max, p1);
            n0 = n0 * 2.0 - 1.0;
            n1 = n1 * 2.0 - 1.0;
        }

        localPos = mix(p0, p1, fr);
        localNorm = normalize(mix(n0, n1, fr));
    }

    frag_pos = vec3(World * vec4(localPos, 1.0));
    tex_uv = uv;

    vec3 N = normalize(vec3(World * vec4(localNorm, 0.0)));
    // Re-orthogonalize the baked tangent frame against the animated normal
    // (Gram-Schmidt) so tangent-space normal maps still light correctly. Fall
    // back to an arbitrary basis when the mesh has no tangents.
    vec3 T;
    if (dot(tangent, tangent) > 1e-6) {
        T = normalize(vec3(World * vec4(tangent, 0.0)));
        T = normalize(T - dot(T, N) * N);
    } else {
        vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
    }
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);

    gl_Position = ProjectionView * vec4(frag_pos, 1.0);
}

// NOTES: Uniform location qualifiers only available in version 4.3 or after

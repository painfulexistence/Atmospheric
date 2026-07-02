#pragma once
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "batch_renderer_2d.hpp"
#include "command_encoder.hpp"
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

// WebGPU 2D batch renderer — mirrors 2d-engine's Renderer class.
// Handles all canvas draw commands (quads, sprites, text) for the WebGPU backend.
// Pipeline is initialized lazily on the first Render() call, once GfxFactory
// has a live WGPUDevice (async browser init).
class GPUCanvasPass {
public:
    static constexpr int MAX_VERTS       = 32768; // 8192 quads × 4
    static constexpr int MAX_INDICES     = 49152; // 8192 quads × 6
    // Full 3D position: WorldCanvasPass feeds world-space vertices through a
    // perspective viewProj, so z must survive to the shader (dropping it
    // projects every world sprite onto the z=0 plane — they vanish).
    static constexpr int FLOATS_PER_VERT = 11;    // x,y,z, u,v, r,g,b,a, texIdx(unused), flags

    GPUCanvasPass() = default;
    ~GPUCanvasPass();

    // Records draw calls into the WGPURenderPassEncoder already open on enc
    // (caller must have called sceneRT->Begin(enc) first and will call End()
    // afterward, or opened a render pass on the swapchain view directly —
    // see toSwapchain below).
    //
    // depthTest selects the pipeline variant used by WorldCanvasPass: depth
    // is tested (read-only, no write) against sceneRT's depth buffer so
    // world-space sprites are occluded by 3D geometry, matching the GL path's
    // glDepthMask(GL_FALSE) behaviour. CanvasPass (screen-space UI) passes
    // false, the default, for the no-depth pipeline.
    //
    // toSwapchain selects the swapchain-format pipeline variant, used by
    // UIPass: it runs after PostProcessPass has already resolved sceneRT
    // (RGBA16Float) to the swapchain (BGRA8Unorm typically), so it must
    // record into a render pass opened on the swapchain view directly rather
    // than sceneRT. Mutually exclusive with depthTest.
    void Render(CommandEncoder* enc,
                const glm::mat4& viewProj,
                const std::vector<BatchDrawCommand>& commands,
                bool depthTest = false,
                bool toSwapchain = false);

    bool IsReady() const { return _pipeline != nullptr; }

private:
    // WGSL source — identical to 2d-engine's QUAD_WGSL
    static constexpr const char* QUAD_WGSL = R"(
struct Uniforms { viewProj: mat4x4<f32> }
@group(0) @binding(0) var<uniform> uni: Uniforms;
@group(1) @binding(0) var tex: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct Vert {
  @location(0) pos:   vec3<f32>,
  @location(1) uv:    vec2<f32>,
  @location(2) color: vec4<f32>,
  @location(3) flags: vec2<f32>,
}
struct VOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv:    vec2<f32>,
  @location(1) color: vec4<f32>,
  @location(2) flags: vec2<f32>,
}

@vertex fn vs(v: Vert) -> VOut {
  return VOut(uni.viewProj * vec4<f32>(v.pos, 1.0), v.uv, v.color, v.flags);
}

@fragment fn fs(in: VOut) -> @location(0) vec4<f32> {
  let flags    = u32(in.flags.y);
  let s        = textureSample(tex, samp, in.uv);
  let masked   = vec4<f32>(in.color.rgb, in.color.a * s.a);
  let textured = s * in.color;
  let result   = select(select(textured, masked, (flags & 2u) != 0u), in.color, (flags & 1u) != 0u);
  return result;
}
)";

    void _init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format);
    WGPUBindGroup _getOrCreateTexBG(uint32_t texID);

    WGPUDevice  _device  = nullptr;
    WGPUQueue   _queue   = nullptr;

    WGPURenderPipeline  _pipeline   = nullptr;
    // Depth-tested variant (read-only, no write) used by WorldCanvasPass so
    // world-space sprites are occluded by 3D geometry already in sceneRT's
    // depth buffer. Shares every other piece of state (buffers, bind groups,
    // texture cache) with the no-depth pipeline above.
    WGPURenderPipeline  _pipelineDepthTest = nullptr;
    // Swapchain-format variant used by UIPass (see Render() doc above).
    // Shares BGLs/buffers/bind groups with the other two pipelines.
    WGPURenderPipeline  _pipelineSwapchain = nullptr;
    WGPUBindGroupLayout _uniformBGL = nullptr;
    WGPUBindGroupLayout _texBGL     = nullptr;
    WGPUBuffer          _vertexBuf  = nullptr;
    WGPUBuffer          _indexBuf   = nullptr;
    WGPUBuffer          _uniformBuf = nullptr;
    WGPUBindGroup       _uniformBG  = nullptr;
    WGPUSampler         _sampler    = nullptr;
    WGPUTexture         _whiteTex   = nullptr;

    std::vector<float>    _verts;
    std::vector<uint32_t> _indices;

    std::unordered_map<uint32_t, WGPUBindGroup> _texBGCache;
};
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

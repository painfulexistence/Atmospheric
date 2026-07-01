#pragma once
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include <vector>

// ── Visibility aliases ────────────────────────────────────────────────────────
namespace wgsl_stage {
    constexpr WGPUShaderStage vert = WGPUShaderStage_Vertex;
    constexpr WGPUShaderStage frag = WGPUShaderStage_Fragment;
    constexpr WGPUShaderStage both = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
}

// ── Bind-group-layout entry descriptor ───────────────────────────────────────
struct GpuBGLEntry {
    uint32_t binding = 0;
    WGPUShaderStage visibility = WGPUShaderStage_Fragment;
    enum class Kind { Uniform, DynamicUniform, Texture, Sampler } kind = Kind::Uniform;
    uint64_t minBindingSize = 0;
};

inline GpuBGLEntry gpuUniform(uint32_t b, WGPUShaderStage v, uint64_t sz) {
    return { b, v, GpuBGLEntry::Kind::Uniform, sz };
}
inline GpuBGLEntry gpuDynUniform(uint32_t b, WGPUShaderStage v, uint64_t sz) {
    return { b, v, GpuBGLEntry::Kind::DynamicUniform, sz };
}
inline GpuBGLEntry gpuTexture(uint32_t b, WGPUShaderStage v = WGPUShaderStage_Fragment) {
    return { b, v, GpuBGLEntry::Kind::Texture, 0 };
}
inline GpuBGLEntry gpuSampler(uint32_t b, WGPUShaderStage v = WGPUShaderStage_Fragment) {
    return { b, v, GpuBGLEntry::Kind::Sampler, 0 };
}

// ── Pipeline result ───────────────────────────────────────────────────────────
// Caller owns the handles; no automatic release on destruction (matches the
// manual-release pattern used for all WebGPU handles in this codebase).
struct GpuPipeline {
    WGPURenderPipeline pipeline = nullptr;
    std::vector<WGPUBindGroupLayout> bgls;
    WGPUBindGroupLayout bgl(size_t i) const { return bgls[i]; }
};

// ── Vertex attribute descriptor ───────────────────────────────────────────────
struct GpuVertexAttr {
    WGPUVertexFormat format;
    uint64_t offset;
    uint32_t shaderLocation;
};

// ── Pipeline builder ──────────────────────────────────────────────────────────
// Build a WGPURenderPipeline + its bind-group layouts in ~5–15 lines instead
// of the ~80–140-line per-pass boilerplate.  Call build() once per pipeline.
//
// Example:
//   auto p = GpuPipelineBuilder(device)
//       .wgsl(MY_WGSL_SRC)
//       .bgl({ gpuUniform(0, wgsl_stage::both, 128),
//              gpuDynUniform(1, wgsl_stage::vert, 64) })
//       .bgl({ gpuTexture(0), gpuSampler(1) })
//       .vertex(56, { {WGPUVertexFormat_Float32x3, 0, 0}, ... })
//       .colorFormat(WGPUTextureFormat_RGBA16Float)
//       .depth(true, WGPUCompareFunction_Less)
//       .cull(WGPUCullMode_Back)
//       .build();
//   _pipeline   = p.pipeline;
//   _uniformBGL = p.bgl(0);
//   _texBGL     = p.bgl(1);
class GpuPipelineBuilder {
public:
    explicit GpuPipelineBuilder(WGPUDevice device) : _device(device) {}

    GpuPipelineBuilder& wgsl(const char* src)         { _src = src; return *this; }
    GpuPipelineBuilder& wgsl(const std::string& src)  { _src = src; return *this; }

    GpuPipelineBuilder& bgl(std::vector<GpuBGLEntry> entries) {
        _bglGroups.push_back({ std::move(entries), nullptr });
        return *this;
    }

    // Reuse an existing bind-group layout instead of creating a new one —
    // needed when building a pipeline variant whose bind groups must stay
    // compatible with a previously built pipeline (e.g. GPUCanvasPass's
    // depth-tested and swapchain-format variants). The borrowed BGL still
    // appears in GpuPipeline::bgls at its group index so indexing stays
    // uniform, but ownership remains with the original creator — do not
    // release it a second time through the returned GpuPipeline.
    GpuPipelineBuilder& bgl(WGPUBindGroupLayout existing) {
        _bglGroups.push_back({ {}, existing });
        return *this;
    }

    GpuPipelineBuilder& vertex(uint64_t stride, std::vector<GpuVertexAttr> attrs) {
        _stride      = stride;
        _vertexAttrs = std::move(attrs);
        return *this;
    }

    GpuPipelineBuilder& colorFormat(WGPUTextureFormat fmt) { _colorFmt = fmt; return *this; }

    // Enable standard src-alpha / one-minus-src-alpha blending.
    GpuPipelineBuilder& blend() { _blend = true; return *this; }

    // Enable depth testing.  writeEnabled=true for opaque, false for transparent.
    GpuPipelineBuilder& depth(bool writeEnabled,
                              WGPUCompareFunction cmp = WGPUCompareFunction_Less) {
        _depthEnabled = true;
        _depthWrite   = writeEnabled;
        _depthCompare = cmp;
        return *this;
    }

    GpuPipelineBuilder& cull(WGPUCullMode mode) { _cullMode = mode; return *this; }

    GpuPipeline build();

private:
    // One bind group slot: either a list of entries to create a fresh BGL
    // from, or a borrowed pre-existing BGL (entries empty, existing set).
    struct BGLGroup {
        std::vector<GpuBGLEntry> entries;
        WGPUBindGroupLayout      existing = nullptr;
    };

    WGPUDevice _device = nullptr;
    std::string _src;
    std::vector<BGLGroup> _bglGroups;
    uint64_t _stride = 0;
    std::vector<GpuVertexAttr> _vertexAttrs;
    WGPUTextureFormat   _colorFmt     = WGPUTextureFormat_RGBA16Float;
    bool                _blend        = false;
    bool                _depthEnabled = false;
    bool                _depthWrite   = false;
    WGPUCompareFunction _depthCompare = WGPUCompareFunction_Less;
    WGPUCullMode        _cullMode     = WGPUCullMode_None;
};

#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

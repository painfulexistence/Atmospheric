#pragma once
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <cstdint>
#include <string>
#include <vector>
#include <webgpu/webgpu.h>

// ── Visibility aliases ────────────────────────────────────────────────────────
namespace wgsl_stage {
    constexpr WGPUShaderStage vert = WGPUShaderStage_Vertex;
    constexpr WGPUShaderStage frag = WGPUShaderStage_Fragment;
    constexpr WGPUShaderStage both = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
}// namespace wgsl_stage

// ── Bind-group-layout entry descriptor ───────────────────────────────────────
struct GpuBGLEntry {
    uint32_t binding = 0;
    WGPUShaderStage visibility = WGPUShaderStage_Fragment;
    enum class Kind {
        Uniform,
        DynamicUniform,
        Texture,
        Sampler,
        DepthTexture,// texture_depth_2d (e.g. shadow map)
        ComparisonSampler,// sampler_comparison (for textureSampleCompare)
        UintTexture3D,// texture_3d<u32> (e.g. micro voxel volume, read via textureLoad)
    } kind = Kind::Uniform;
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
inline GpuBGLEntry gpuDepthTexture(uint32_t b, WGPUShaderStage v = WGPUShaderStage_Fragment) {
    return { b, v, GpuBGLEntry::Kind::DepthTexture, 0 };
}
inline GpuBGLEntry gpuCompareSampler(uint32_t b, WGPUShaderStage v = WGPUShaderStage_Fragment) {
    return { b, v, GpuBGLEntry::Kind::ComparisonSampler, 0 };
}
inline GpuBGLEntry gpuUintTexture3D(uint32_t b, WGPUShaderStage v = WGPUShaderStage_Fragment) {
    return { b, v, GpuBGLEntry::Kind::UintTexture3D, 0 };
}

// Correctly-defaulted WGPUSamplerDescriptor. Zero-initializing the struct is
// INVALID: maxAnisotropy 0 fails Dawn validation ("Max anisotropy (0.000000)
// is less than 1"). Start from this and override the fields you need.
inline WGPUSamplerDescriptor gpuSamplerDesc() {
    WGPUSamplerDescriptor d{};
    d.addressModeU = WGPUAddressMode_ClampToEdge;
    d.addressModeV = WGPUAddressMode_ClampToEdge;
    d.addressModeW = WGPUAddressMode_ClampToEdge;
    d.magFilter = WGPUFilterMode_Nearest;
    d.minFilter = WGPUFilterMode_Nearest;
    d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    d.lodMinClamp = 0.0f;
    d.lodMaxClamp = 32.0f;// WebGPU spec default
    d.maxAnisotropy = 1;// must be >= 1
    return d;
}

// ── Pipeline result ───────────────────────────────────────────────────────────
// Caller owns the handles; no automatic release on destruction (matches the
// manual-release pattern used for all WebGPU handles in this codebase).
struct GpuPipeline {
    WGPURenderPipeline pipeline = nullptr;
    std::vector<WGPUBindGroupLayout> bgls;
    WGPUBindGroupLayout bgl(size_t i) const {
        return bgls[i];
    }
};

// ── Vertex attribute descriptor ───────────────────────────────────────────────
struct GpuVertexAttr {
    WGPUVertexFormat format;
    uint64_t offset;
    uint32_t shaderLocation;
};

// ── Bind group builder ────────────────────────────────────────────────────────
// Companion to GpuPipelineBuilder for the other half of the boilerplate:
// creating the WGPUBindGroup that matches a BGL. Caller owns the result.
//
//   _uniformBG = GpuBindGroupBuilder(device, _uniformBGL)
//       .buffer(0, _frameUniformBuf, FRAME_UNIFORM_SIZE)
//       .buffer(1, _drawUniformBuf,  DRAW_UNIFORM_SIZE)   // dyn-offset: size = one slot
//       .build();
//
//   texBG = GpuBindGroupBuilder(device, _texBGL)
//       .texture(0, rawTex)      // transient view created + released internally
//       .sampler(1, _sampler)
//       .build();
class GpuBindGroupBuilder {
public:
    GpuBindGroupBuilder(WGPUDevice device, WGPUBindGroupLayout layout) : _device(device), _layout(layout) {
    }

    GpuBindGroupBuilder& buffer(uint32_t binding, WGPUBuffer buf, uint64_t size, uint64_t offset = 0) {
        WGPUBindGroupEntry e{};
        e.binding = binding;
        e.buffer = buf;
        e.size = size;
        e.offset = offset;
        _entries.push_back(e);
        return *this;
    }

    // Binds the texture's default full view; the transient WGPUTextureView is
    // created here and released in build().
    GpuBindGroupBuilder& texture(uint32_t binding, WGPUTexture tex) {
        WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
        WGPUBindGroupEntry e{};
        e.binding = binding;
        e.textureView = view;
        _entries.push_back(e);
        _transientViews.push_back(view);
        return *this;
    }

    // Binds a caller-owned view (e.g. a persistent shadow-map view); the
    // caller keeps ownership and must outlive the bind group.
    GpuBindGroupBuilder& textureView(uint32_t binding, WGPUTextureView view) {
        WGPUBindGroupEntry e{};
        e.binding = binding;
        e.textureView = view;
        _entries.push_back(e);
        return *this;
    }

    GpuBindGroupBuilder& sampler(uint32_t binding, WGPUSampler s) {
        WGPUBindGroupEntry e{};
        e.binding = binding;
        e.sampler = s;
        _entries.push_back(e);
        return *this;
    }

    WGPUBindGroup build() {
        WGPUBindGroupDescriptor d{};
        d.layout = _layout;
        d.entryCount = (uint32_t)_entries.size();
        d.entries = _entries.data();
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(_device, &d);
        for (WGPUTextureView v : _transientViews)
            wgpuTextureViewRelease(v);
        _transientViews.clear();
        return bg;
    }

private:
    WGPUDevice _device = nullptr;
    WGPUBindGroupLayout _layout = nullptr;
    std::vector<WGPUBindGroupEntry> _entries;
    std::vector<WGPUTextureView> _transientViews;
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
    explicit GpuPipelineBuilder(WGPUDevice device) : _device(device) {
    }

    GpuPipelineBuilder& wgsl(const char* src) {
        _src = src;
        return *this;
    }
    GpuPipelineBuilder& wgsl(const std::string& src) {
        _src = src;
        return *this;
    }

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
        _stride = stride;
        _vertexAttrs = std::move(attrs);
        return *this;
    }

    GpuPipelineBuilder& colorFormat(WGPUTextureFormat fmt) {
        _colorFmt = fmt;
        return *this;
    }

    // Enable standard src-alpha / one-minus-src-alpha blending.
    GpuPipelineBuilder& blend() {
        _blend = true;
        return *this;
    }

    // Enable depth testing.  writeEnabled=true for opaque, false for transparent.
    GpuPipelineBuilder& depth(bool writeEnabled, WGPUCompareFunction cmp = WGPUCompareFunction_Less) {
        _depthEnabled = true;
        _depthWrite = writeEnabled;
        _depthCompare = cmp;
        return *this;
    }

    GpuPipelineBuilder& cull(WGPUCullMode mode) {
        _cullMode = mode;
        return *this;
    }

    // Depth-only pipeline (shadow maps): no fragment stage, no color target.
    // The WGSL only needs a vs entry point. Must be combined with depth().
    GpuPipelineBuilder& depthOnly() {
        _depthOnlyPipeline = true;
        return *this;
    }

    // MSAA sample count — must match the render pass's attachments (e.g.
    // sceneRT->GetNumSamples() for pipelines drawing into sceneRT).
    GpuPipelineBuilder& multisample(uint32_t count) {
        _sampleCount = count;
        return *this;
    }

    GpuPipeline build();

private:
    // One bind group slot: either a list of entries to create a fresh BGL
    // from, or a borrowed pre-existing BGL (entries empty, existing set).
    struct BGLGroup {
        std::vector<GpuBGLEntry> entries;
        WGPUBindGroupLayout existing = nullptr;
    };

    WGPUDevice _device = nullptr;
    std::string _src;
    std::vector<BGLGroup> _bglGroups;
    uint64_t _stride = 0;
    std::vector<GpuVertexAttr> _vertexAttrs;
    WGPUTextureFormat _colorFmt = WGPUTextureFormat_RGBA16Float;
    bool _blend = false;
    bool _depthEnabled = false;
    bool _depthWrite = false;
    WGPUCompareFunction _depthCompare = WGPUCompareFunction_Less;
    WGPUCullMode _cullMode = WGPUCullMode_None;
    bool _depthOnlyPipeline = false;
    uint32_t _sampleCount = 1;
};

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

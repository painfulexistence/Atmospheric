#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_pipeline.hpp"

GpuPipeline GpuPipelineBuilder::build() {
    // Compile shader module
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code        = { _src.c_str(), WGPU_STRLEN };
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(_device, &shaderDesc);

    // Build each BGL
    GpuPipeline result;
    result.bgls.reserve(_bglEntries.size());
    std::vector<WGPUBindGroupLayout> layouts;
    layouts.reserve(_bglEntries.size());

    for (const auto& entries : _bglEntries) {
        std::vector<WGPUBindGroupLayoutEntry> wgpuEntries;
        wgpuEntries.reserve(entries.size());
        for (const auto& e : entries) {
            WGPUBindGroupLayoutEntry we{};
            we.binding    = e.binding;
            we.visibility = e.visibility;
            switch (e.kind) {
                case GpuBGLEntry::Kind::Uniform:
                    we.buffer.type           = WGPUBufferBindingType_Uniform;
                    we.buffer.minBindingSize  = e.minBindingSize;
                    break;
                case GpuBGLEntry::Kind::DynamicUniform:
                    we.buffer.type             = WGPUBufferBindingType_Uniform;
                    we.buffer.hasDynamicOffset = true;
                    we.buffer.minBindingSize   = e.minBindingSize;
                    break;
                case GpuBGLEntry::Kind::Texture:
                    we.texture.sampleType    = WGPUTextureSampleType_Float;
                    we.texture.viewDimension = WGPUTextureViewDimension_2D;
                    we.texture.multisampled  = false;
                    break;
                case GpuBGLEntry::Kind::Sampler:
                    we.sampler.type = WGPUSamplerBindingType_Filtering;
                    break;
            }
            wgpuEntries.push_back(we);
        }
        WGPUBindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = (uint32_t)wgpuEntries.size();
        bglDesc.entries    = wgpuEntries.data();
        WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(_device, &bglDesc);
        result.bgls.push_back(bgl);
        layouts.push_back(bgl);
    }

    // Build pipeline layout
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = (uint32_t)layouts.size();
    plDesc.bindGroupLayouts     = layouts.empty() ? nullptr : layouts.data();
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(_device, &plDesc);

    // Blend state (standard src-alpha / one-minus-src-alpha)
    WGPUBlendComponent blendColor{ WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                                   WGPUBlendFactor_OneMinusSrcAlpha };
    WGPUBlendComponent blendAlpha{ WGPUBlendOperation_Add, WGPUBlendFactor_One,
                                   WGPUBlendFactor_OneMinusSrcAlpha };
    WGPUBlendState blendState{ blendColor, blendAlpha };

    WGPUColorTargetState colorTarget{};
    colorTarget.format    = _colorFmt;
    colorTarget.blend     = _blend ? &blendState : nullptr;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag{};
    frag.module      = shader;
    frag.entryPoint  = { "fs", WGPU_STRLEN };
    frag.targetCount = 1;
    frag.targets     = &colorTarget;

    // Vertex buffer layout (optional — fullscreen-triangle passes skip this)
    std::vector<WGPUVertexAttribute> wgpuAttrs;
    WGPUVertexBufferLayout vbl{};
    if (!_vertexAttrs.empty()) {
        wgpuAttrs.reserve(_vertexAttrs.size());
        for (const auto& a : _vertexAttrs) {
            WGPUVertexAttribute wa{};
            wa.format         = a.format;
            wa.offset         = a.offset;
            wa.shaderLocation = a.shaderLocation;
            wgpuAttrs.push_back(wa);
        }
        vbl.arrayStride    = _stride;
        vbl.stepMode       = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = (uint32_t)wgpuAttrs.size();
        vbl.attributes     = wgpuAttrs.data();
    }

    // Depth stencil (optional)
    WGPUDepthStencilState depthStencil{};
    if (_depthEnabled) {
        depthStencil.format               = WGPUTextureFormat_Depth32Float;
        depthStencil.depthWriteEnabled    = _depthWrite ? WGPUOptionalBool_True
                                                        : WGPUOptionalBool_False;
        depthStencil.depthCompare         = _depthCompare;
        depthStencil.stencilFront.compare = WGPUCompareFunction_Always;
        depthStencil.stencilBack.compare  = WGPUCompareFunction_Always;
        depthStencil.stencilReadMask      = 0xFFFFFFFFu;
        depthStencil.stencilWriteMask     = 0xFFFFFFFFu;
    }

    WGPURenderPipelineDescriptor pd{};
    pd.layout              = pipelineLayout;
    pd.vertex.module       = shader;
    pd.vertex.entryPoint   = { "vs", WGPU_STRLEN };
    pd.vertex.bufferCount  = _vertexAttrs.empty() ? 0u : 1u;
    pd.vertex.buffers      = _vertexAttrs.empty() ? nullptr : &vbl;
    pd.fragment            = &frag;
    pd.depthStencil        = _depthEnabled ? &depthStencil : nullptr;
    pd.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode  = _cullMode;
    pd.multisample.count   = 1;
    pd.multisample.mask    = 0xFFFFFFFFu;
    result.pipeline = wgpuDeviceCreateRenderPipeline(_device, &pd);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shader);
    return result;
}

#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

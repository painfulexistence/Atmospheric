#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_pipeline.hpp"

GpuPipeline GpuPipelineBuilder::build() {
    // Compile shader module
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = { _src.c_str(), WGPU_STRLEN };
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(_device, &shaderDesc);

    // Build (or borrow) each BGL
    GpuPipeline result;
    result.bgls.reserve(_bglGroups.size());
    std::vector<WGPUBindGroupLayout> layouts;
    layouts.reserve(_bglGroups.size());

    for (const auto& group : _bglGroups) {
        if (group.existing) {
            // Borrowed BGL: reference it in the pipeline layout and expose it
            // via result.bgls for uniform indexing, but ownership stays with
            // the original creator (see the bgl(WGPUBindGroupLayout) doc).
            result.bgls.push_back(group.existing);
            layouts.push_back(group.existing);
            continue;
        }
        const auto& entries = group.entries;
        std::vector<WGPUBindGroupLayoutEntry> wgpuEntries;
        wgpuEntries.reserve(entries.size());
        for (const auto& e : entries) {
            WGPUBindGroupLayoutEntry we{};
            we.binding = e.binding;
            we.visibility = e.visibility;
            switch (e.kind) {
            case GpuBGLEntry::Kind::Uniform:
                we.buffer.type = WGPUBufferBindingType_Uniform;
                we.buffer.minBindingSize = e.minBindingSize;
                break;
            case GpuBGLEntry::Kind::DynamicUniform:
                we.buffer.type = WGPUBufferBindingType_Uniform;
                we.buffer.hasDynamicOffset = true;
                we.buffer.minBindingSize = e.minBindingSize;
                break;
            case GpuBGLEntry::Kind::Texture:
                we.texture.sampleType = WGPUTextureSampleType_Float;
                we.texture.viewDimension = WGPUTextureViewDimension_2D;
                we.texture.multisampled = false;
                break;
            case GpuBGLEntry::Kind::Sampler:
                we.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case GpuBGLEntry::Kind::DepthTexture:
                we.texture.sampleType = WGPUTextureSampleType_Depth;
                we.texture.viewDimension = WGPUTextureViewDimension_2D;
                we.texture.multisampled = false;
                break;
            case GpuBGLEntry::Kind::ComparisonSampler:
                we.sampler.type = WGPUSamplerBindingType_Comparison;
                break;
            case GpuBGLEntry::Kind::UintTexture3D:
                we.texture.sampleType = WGPUTextureSampleType_Uint;
                we.texture.viewDimension = WGPUTextureViewDimension_3D;
                we.texture.multisampled = false;
                break;
            }
            wgpuEntries.push_back(we);
        }
        WGPUBindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = (uint32_t)wgpuEntries.size();
        bglDesc.entries = wgpuEntries.data();
        WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(_device, &bglDesc);
        result.bgls.push_back(bgl);
        layouts.push_back(bgl);
    }

    // Build pipeline layout
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = (uint32_t)layouts.size();
    plDesc.bindGroupLayouts = layouts.empty() ? nullptr : layouts.data();
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(_device, &plDesc);

    // Blend state — factor pairs mirror the GL BatchRenderer2D::Flush switch
    // (batch_renderer_2d.cpp) so the two backends composite identically.
    // Alpha channel uses (1, 1-src.a) except for Multiply / Screen where it
    // follows the color factors to keep the output alpha meaningful when the
    // result is later blended into another target.
    WGPUBlendState blendState{};
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    switch (_blend) {
    case GpuPipelineBuilder::Blend::None:
        break;// blendState unused (colorTarget.blend = nullptr below)
    case GpuPipelineBuilder::Blend::Alpha:
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        break;
    case GpuPipelineBuilder::Blend::Additive:
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One };
        break;
    case GpuPipelineBuilder::Blend::Multiply:
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_Zero };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_Zero };
        break;
    case GpuPipelineBuilder::Blend::Screen:
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrc };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrc };
        break;
    case GpuPipelineBuilder::Blend::Premultiplied:
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha };
        break;
    }

    WGPUColorTargetState colorTarget{};
    colorTarget.format = _colorFmt;
    colorTarget.blend = (_blend == GpuPipelineBuilder::Blend::None) ? nullptr : &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag{};
    frag.module = shader;
    frag.entryPoint = { "fs", WGPU_STRLEN };
    frag.targetCount = 1;
    frag.targets = &colorTarget;

    // Vertex buffer layout (optional — fullscreen-triangle passes skip this)
    std::vector<WGPUVertexAttribute> wgpuAttrs;
    WGPUVertexBufferLayout vbl{};
    if (!_vertexAttrs.empty()) {
        wgpuAttrs.reserve(_vertexAttrs.size());
        for (const auto& a : _vertexAttrs) {
            WGPUVertexAttribute wa{};
            wa.format = a.format;
            wa.offset = a.offset;
            wa.shaderLocation = a.shaderLocation;
            wgpuAttrs.push_back(wa);
        }
        vbl.arrayStride = _stride;
        vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = (uint32_t)wgpuAttrs.size();
        vbl.attributes = wgpuAttrs.data();
    }

    // Depth stencil (optional)
    WGPUDepthStencilState depthStencil{};
    if (_depthEnabled) {
        depthStencil.format = WGPUTextureFormat_Depth32Float;
        depthStencil.depthWriteEnabled = _depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = _depthCompare;
        depthStencil.stencilFront.compare = WGPUCompareFunction_Always;
        depthStencil.stencilBack.compare = WGPUCompareFunction_Always;
        depthStencil.stencilReadMask = 0xFFFFFFFFu;
        depthStencil.stencilWriteMask = 0xFFFFFFFFu;
    }

    WGPURenderPipelineDescriptor pd{};
    pd.layout = pipelineLayout;
    pd.vertex.module = shader;
    pd.vertex.entryPoint = { "vs", WGPU_STRLEN };
    pd.vertex.bufferCount = _vertexAttrs.empty() ? 0u : 1u;
    pd.vertex.buffers = _vertexAttrs.empty() ? nullptr : &vbl;
    pd.fragment = _depthOnlyPipeline ? nullptr : &frag;
    pd.depthStencil = _depthEnabled ? &depthStencil : nullptr;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = _cullMode;
    pd.multisample.count = _sampleCount;
    pd.multisample.mask = 0xFFFFFFFFu;
    result.pipeline = wgpuDeviceCreateRenderPipeline(_device, &pd);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shader);
    return result;
}

#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

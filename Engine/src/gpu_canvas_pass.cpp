#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#include "gpu_pipeline.hpp"
#include "gfx_factory.hpp"
#include <glm/gtc/type_ptr.hpp>

GPUCanvasPass::~GPUCanvasPass() {
    for (auto& [id, bg] : _texBGCache) wgpuBindGroupRelease(bg);
    if (_uniformBG)  wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL)     wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline)   wgpuRenderPipelineRelease(_pipeline);
    if (_pipelineDepthTest) wgpuRenderPipelineRelease(_pipelineDepthTest);
    if (_pipelineSwapchain) wgpuRenderPipelineRelease(_pipelineSwapchain);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf)  wgpuBufferRelease(_vertexBuf);
    if (_indexBuf)   wgpuBufferRelease(_indexBuf);
    if (_whiteTex)   wgpuTextureRelease(_whiteTex);
    if (_sampler)    wgpuSamplerRelease(_sampler);
}

void GPUCanvasPass::_init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    _device = device;
    _queue  = queue;

    // ── Buffers ────────────────────────────────────────────────────────────
    {
        WGPUBufferDescriptor d{};
        d.size  = (uint64_t)MAX_VERTS * FLOATS_PER_VERT * sizeof(float);
        d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        _vertexBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    {
        WGPUBufferDescriptor d{};
        d.size  = (uint64_t)MAX_INDICES * sizeof(uint32_t);
        d.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        _indexBuf = wgpuDeviceCreateBuffer(device, &d);
    }
    {
        WGPUBufferDescriptor d{};
        d.size  = 64; // mat4x4<f32>
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }

    // ── Sampler ────────────────────────────────────────────────────────────
    {
        WGPUSamplerDescriptor d{};
        d.minFilter    = WGPUFilterMode_Nearest;
        d.magFilter    = WGPUFilterMode_Nearest;
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.addressModeU = WGPUAddressMode_ClampToEdge;
        d.addressModeV = WGPUAddressMode_ClampToEdge;
        _sampler = wgpuDeviceCreateSampler(device, &d);
    }

    // ── White 1×1 texture ──────────────────────────────────────────────────
    {
        WGPUTextureDescriptor d{};
        d.size          = { 1, 1, 1 };
        d.format        = WGPUTextureFormat_RGBA8Unorm;
        d.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension     = WGPUTextureDimension_2D;
        d.mipLevelCount = 1;
        d.sampleCount   = 1;
        _whiteTex = wgpuDeviceCreateTexture(device, &d);
        const uint8_t white[4] = { 255, 255, 255, 255 };
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _whiteTex;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, white, 4, &layout, &extent);
    }

    // ── First pipeline: no depth, blend on (2D screenspace canvas) ────────
    // Vertex layout: pos(2f) uv(2f) color(4f) flags(2f) = 10 floats = 40 B.
    auto p = GpuPipelineBuilder(device)
        .wgsl(QUAD_WGSL)
        .bgl({ gpuUniform(0, wgsl_stage::vert, 64) })
        .bgl({ gpuTexture(0), gpuSampler(1) })
        .vertex((uint64_t)FLOATS_PER_VERT * sizeof(float),
                { {WGPUVertexFormat_Float32x2,  0, 0},
                  {WGPUVertexFormat_Float32x2,  8, 1},
                  {WGPUVertexFormat_Float32x4, 16, 2},
                  {WGPUVertexFormat_Float32x2, 32, 3} })
        .colorFormat(format).blend().build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL     = p.bgl(1);

    // ── Uniform bind group (must come after build so _uniformBGL is valid) ──
    {
        WGPUBindGroupEntry e{};
        e.binding = 0;
        e.buffer  = _uniformBuf;
        e.size    = 64;
        WGPUBindGroupDescriptor d{};
        d.layout     = _uniformBGL;
        d.entryCount = 1;
        d.entries    = &e;
        _uniformBG = wgpuDeviceCreateBindGroup(device, &d);
    }

    // ── Depth-tested variant (WorldCanvasPass): read-only LessEqual ────────
    // Must reuse the same BGLs so bind groups created above stay compatible.
    {
        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code        = { QUAD_WGSL, WGPU_STRLEN };
        WGPUShaderModuleDescriptor shDesc{};
        shDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
        WGPUShaderModule sh = wgpuDeviceCreateShaderModule(device, &shDesc);

        WGPUBindGroupLayout bgls[2] = { _uniformBGL, _texBGL };
        WGPUPipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 2;
        plDesc.bindGroupLayouts     = bgls;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(device, &plDesc);

        WGPUVertexAttribute attrs[4]{};
        attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset =  0; attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2; attrs[1].offset =  8; attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x4; attrs[2].offset = 16; attrs[2].shaderLocation = 2;
        attrs[3].format = WGPUVertexFormat_Float32x2; attrs[3].offset = 32; attrs[3].shaderLocation = 3;
        WGPUVertexBufferLayout vbl{};
        vbl.arrayStride    = (uint64_t)FLOATS_PER_VERT * sizeof(float);
        vbl.stepMode       = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 4;
        vbl.attributes     = attrs;

        WGPUBlendComponent bc{ WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                               WGPUBlendFactor_OneMinusSrcAlpha };
        WGPUBlendComponent ba{ WGPUBlendOperation_Add, WGPUBlendFactor_One,
                               WGPUBlendFactor_OneMinusSrcAlpha };
        WGPUBlendState blend{ bc, ba };
        WGPUColorTargetState ct{};
        ct.format    = format;
        ct.blend     = &blend;
        ct.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState frag{};
        frag.module      = sh;
        frag.entryPoint  = { "fs", WGPU_STRLEN };
        frag.targetCount = 1;
        frag.targets     = &ct;

        WGPUDepthStencilState ds{};
        ds.format               = WGPUTextureFormat_Depth32Float;
        ds.depthWriteEnabled    = WGPUOptionalBool_False;
        ds.depthCompare         = WGPUCompareFunction_LessEqual;
        ds.stencilFront.compare = WGPUCompareFunction_Always;
        ds.stencilBack.compare  = WGPUCompareFunction_Always;
        ds.stencilReadMask      = 0xFFFFFFFFu;
        ds.stencilWriteMask     = 0xFFFFFFFFu;

        WGPURenderPipelineDescriptor pd{};
        pd.layout              = pl;
        pd.vertex.module       = sh;
        pd.vertex.entryPoint   = { "vs", WGPU_STRLEN };
        pd.vertex.bufferCount  = 1;
        pd.vertex.buffers      = &vbl;
        pd.fragment            = &frag;
        pd.depthStencil        = &ds;
        pd.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.frontFace = WGPUFrontFace_CCW;
        pd.primitive.cullMode  = WGPUCullMode_None;
        pd.multisample.count   = 1;
        pd.multisample.mask    = 0xFFFFFFFFu;
        _pipelineDepthTest = wgpuDeviceCreateRenderPipeline(device, &pd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(sh);
    }

    // ── Swapchain-format variant (UIPass): no depth, blend on, but targets ──
    // GfxFactory::GetSwapchainFormat() instead of sceneRT's HDR format, since
    // UIPass runs after PostProcessPass has already resolved sceneRT to the
    // swapchain. Must reuse the same BGLs so bind groups created above stay
    // compatible.
    {
        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code        = { QUAD_WGSL, WGPU_STRLEN };
        WGPUShaderModuleDescriptor shDesc{};
        shDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
        WGPUShaderModule sh = wgpuDeviceCreateShaderModule(device, &shDesc);

        WGPUBindGroupLayout bgls[2] = { _uniformBGL, _texBGL };
        WGPUPipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 2;
        plDesc.bindGroupLayouts     = bgls;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(device, &plDesc);

        WGPUVertexAttribute attrs[4]{};
        attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset =  0; attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2; attrs[1].offset =  8; attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x4; attrs[2].offset = 16; attrs[2].shaderLocation = 2;
        attrs[3].format = WGPUVertexFormat_Float32x2; attrs[3].offset = 32; attrs[3].shaderLocation = 3;
        WGPUVertexBufferLayout vbl{};
        vbl.arrayStride    = (uint64_t)FLOATS_PER_VERT * sizeof(float);
        vbl.stepMode       = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 4;
        vbl.attributes     = attrs;

        WGPUBlendComponent bc{ WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                               WGPUBlendFactor_OneMinusSrcAlpha };
        WGPUBlendComponent ba{ WGPUBlendOperation_Add, WGPUBlendFactor_One,
                               WGPUBlendFactor_OneMinusSrcAlpha };
        WGPUBlendState blend{ bc, ba };
        WGPUColorTargetState ct{};
        ct.format    = GfxFactory::GetSwapchainFormat();
        ct.blend     = &blend;
        ct.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState frag{};
        frag.module      = sh;
        frag.entryPoint  = { "fs", WGPU_STRLEN };
        frag.targetCount = 1;
        frag.targets     = &ct;

        WGPURenderPipelineDescriptor pd{};
        pd.layout              = pl;
        pd.vertex.module       = sh;
        pd.vertex.entryPoint   = { "vs", WGPU_STRLEN };
        pd.vertex.bufferCount  = 1;
        pd.vertex.buffers      = &vbl;
        pd.fragment            = &frag;
        pd.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.frontFace = WGPUFrontFace_CCW;
        pd.primitive.cullMode  = WGPUCullMode_None;
        pd.multisample.count   = 1;
        pd.multisample.mask    = 0xFFFFFFFFu;
        _pipelineSwapchain = wgpuDeviceCreateRenderPipeline(device, &pd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(sh);
    }

    _verts.reserve((size_t)MAX_VERTS * FLOATS_PER_VERT);
    _indices.reserve((size_t)MAX_INDICES);
}

WGPUBindGroup GPUCanvasPass::_getOrCreateTexBG(uint32_t texID) {
    auto it = _texBGCache.find(texID);
    if (it != _texBGCache.end()) return it->second;

    WGPUTexture rawTex = (texID == 0) ? _whiteTex : GfxFactory::GetWGPUTexture(texID);
    if (!rawTex) rawTex = _whiteTex;

    WGPUTextureView view = wgpuTextureCreateView(rawTex, nullptr);

    WGPUBindGroupEntry e[2]{};
    e[0].binding     = 0;
    e[0].textureView = view;
    e[1].binding     = 1;
    e[1].sampler     = _sampler;
    WGPUBindGroupDescriptor d{};
    d.layout     = _texBGL;
    d.entryCount = 2;
    d.entries    = e;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(_device, &d);

    wgpuTextureViewRelease(view);
    _texBGCache[texID] = bg;
    return bg;
}

void GPUCanvasPass::Render(CommandEncoder* enc,
                            const glm::mat4& viewProj,
                            const std::vector<BatchDrawCommand>& commands,
                            bool depthTest,
                            bool toSwapchain) {
    // Lazy init: wait until GfxFactory has a live device
    if (!_pipeline) {
        WGPUDevice dev = GfxFactory::GetWebGPUDevice();
        WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
        if (!dev) return;
        _init(dev, q, WGPUTextureFormat_RGBA16Float); // sceneRT's HDR format
    }

    if (commands.empty()) return;

    // ── Build CPU vertex/index staging and per-texture batch list ─────────
    struct DrawBatch { uint32_t texID; uint32_t idxOffset; uint32_t idxCount; };
    std::vector<DrawBatch> batches;

    _verts.clear();
    _indices.clear();

    uint32_t curTex  = UINT32_MAX;
    int      vertOff = 0;
    uint32_t idxOff  = 0;

    for (const auto& cmd : commands) {
        if (cmd.textureID != curTex) {
            curTex = cmd.textureID;
            batches.push_back({ curTex, idxOff, 0u });
        }

        // flags: 1=solid color (no texture), 0=textured
        const float flags = (cmd.textureID == 0) ? 1.0f : 0.0f;

        for (const auto& bv : cmd.vertices) {
            _verts.push_back(bv.position.x);
            _verts.push_back(bv.position.y);
            _verts.push_back(bv.uv.x);
            _verts.push_back(bv.uv.y);
            _verts.push_back(bv.color.r);
            _verts.push_back(bv.color.g);
            _verts.push_back(bv.color.b);
            _verts.push_back(bv.color.a);
            _verts.push_back(0.0f);   // texIdx slot (unused — single-tex batching)
            _verts.push_back(flags);
        }
        for (uint32_t idx : cmd.indices)
            _indices.push_back(idx + static_cast<uint32_t>(vertOff));

        batches.back().idxCount += static_cast<uint32_t>(cmd.indices.size());
        vertOff += static_cast<int>(cmd.vertices.size());
        idxOff  += static_cast<uint32_t>(cmd.indices.size());
    }

    // ── Upload all data before beginning the render pass ──────────────────
    // wgpuQueueWriteBuffer is ordered before any subsequent submit, so the
    // data is guaranteed to be on the GPU when the render pass executes.
    wgpuQueueWriteBuffer(_queue, _uniformBuf, 0,
                         glm::value_ptr(viewProj), 64);
    wgpuQueueWriteBuffer(_queue, _vertexBuf, 0,
                         _verts.data(), _verts.size() * sizeof(float));
    wgpuQueueWriteBuffer(_queue, _indexBuf, 0,
                         _indices.data(), _indices.size() * sizeof(uint32_t));

    // ── Record into the render pass already opened by the caller ──────────
    // (caller must have called sceneRT->Begin(enc) before this and will call
    // End() afterward — we do not own the pass or command-buffer lifecycle.)
    auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
    WGPURenderPassEncoder pass = gpuEnc->pass;
    if (!pass) return;

    wgpuRenderPassEncoderSetPipeline(pass, toSwapchain ? _pipelineSwapchain
                                          : depthTest   ? _pipelineDepthTest
                                                        : _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _uniformBG, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, _vertexBuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, _indexBuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    for (const auto& batch : batches) {
        if (batch.idxCount == 0) continue;
        WGPUBindGroup texBG = _getOrCreateTexBG(batch.texID);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBG, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, batch.idxCount, 1, batch.idxOffset, 0, 0);
    }
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

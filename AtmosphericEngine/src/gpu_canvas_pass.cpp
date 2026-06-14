#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#include "gfx_factory.hpp"
#include <glm/gtc/type_ptr.hpp>

GPUCanvasPass::~GPUCanvasPass() {
    for (auto& [id, bg] : _texBGCache) wgpuBindGroupRelease(bg);
    if (_uniformBG)  wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL)     wgpuBindGroupLayoutRelease(_texBGL);
    if (_pipeline)   wgpuRenderPipelineRelease(_pipeline);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    if (_vertexBuf)  wgpuBufferRelease(_vertexBuf);
    if (_indexBuf)   wgpuBufferRelease(_indexBuf);
    if (_whiteTex)   wgpuTextureRelease(_whiteTex);
    if (_sampler)    wgpuSamplerRelease(_sampler);
}

void GPUCanvasPass::_init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    _device = device;
    _queue  = queue;

    // ── Shader module ──────────────────────────────────────────────────────
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code        = { QUAD_WGSL, WGPU_STRLEN };
    WGPUShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shaderDesc);

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

    // ── Bind group layouts ─────────────────────────────────────────────────
    {
        WGPUBindGroupLayoutEntry e{};
        e.binding               = 0;
        e.visibility            = WGPUShaderStage_Vertex;
        e.buffer.type           = WGPUBufferBindingType_Uniform;
        e.buffer.minBindingSize = 64;
        WGPUBindGroupLayoutDescriptor d{};
        d.entryCount = 1;
        d.entries    = &e;
        _uniformBGL = wgpuDeviceCreateBindGroupLayout(device, &d);
    }
    {
        WGPUBindGroupLayoutEntry e[2]{};
        e[0].binding               = 0;
        e[0].visibility            = WGPUShaderStage_Fragment;
        e[0].texture.sampleType    = WGPUTextureSampleType_Float;
        e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[0].texture.multisampled  = false;
        e[1].binding               = 1;
        e[1].visibility            = WGPUShaderStage_Fragment;
        e[1].sampler.type          = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor d{};
        d.entryCount = 2;
        d.entries    = e;
        _texBGL = wgpuDeviceCreateBindGroupLayout(device, &d);
    }

    // ── Uniform bind group ─────────────────────────────────────────────────
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

    // ── Pipeline layout ────────────────────────────────────────────────────
    WGPUBindGroupLayout layouts[2] = { _uniformBGL, _texBGL };
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts     = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // ── Vertex buffer layout (stride 40 = 10 floats) ──────────────────────
    WGPUVertexAttribute attrs[4]{};
    attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset =  0; attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2; attrs[1].offset =  8; attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x4; attrs[2].offset = 16; attrs[2].shaderLocation = 2;
    attrs[3].format = WGPUVertexFormat_Float32x2; attrs[3].offset = 32; attrs[3].shaderLocation = 3;
    WGPUVertexBufferLayout vbl{};
    vbl.arrayStride    = (uint64_t)FLOATS_PER_VERT * sizeof(float); // 40
    vbl.stepMode       = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 4;
    vbl.attributes     = attrs;

    // ── Blend state (standard src-alpha / one-minus-src-alpha) ────────────
    WGPUBlendComponent blendColor{ WGPUBlendOperation_Add,
                                    WGPUBlendFactor_SrcAlpha,
                                    WGPUBlendFactor_OneMinusSrcAlpha };
    WGPUBlendComponent blendAlpha{ WGPUBlendOperation_Add,
                                    WGPUBlendFactor_One,
                                    WGPUBlendFactor_OneMinusSrcAlpha };
    WGPUBlendState blend{ blendColor, blendAlpha };
    WGPUColorTargetState colorTarget{};
    colorTarget.format    = format;
    colorTarget.blend     = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag{};
    frag.module      = shader;
    frag.entryPoint  = { "fs", WGPU_STRLEN };
    frag.targetCount = 1;
    frag.targets     = &colorTarget;

    // ── Render pipeline ────────────────────────────────────────────────────
    WGPURenderPipelineDescriptor pd{};
    pd.layout             = pipelineLayout;
    pd.vertex.module      = shader;
    pd.vertex.entryPoint  = { "vs", WGPU_STRLEN };
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers     = &vbl;
    pd.fragment           = &frag;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode  = WGPUCullMode_None;
    pd.multisample.count  = 1;
    pd.multisample.mask   = 0xFFFFFFFFu;
    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pd);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shader);

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

void GPUCanvasPass::Render(WGPUTextureView targetView,
                            const glm::mat4& viewProj,
                            const std::vector<BatchDrawCommand>& commands) {
    // Lazy init: wait until GfxFactory has a live device
    if (!_pipeline) {
        WGPUDevice dev = GfxFactory::GetWebGPUDevice();
        WGPUQueue  q   = GfxFactory::GetWebGPUQueue();
        if (!dev) return;
        _init(dev, q, GfxFactory::GetSwapchainFormat());
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

    // ── Record and submit command buffer ──────────────────────────────────
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(_device, nullptr);

    WGPURenderPassColorAttachment ca{};
    ca.view       = targetView;
    ca.loadOp     = WGPULoadOp_Clear;
    ca.storeOp    = WGPUStoreOp_Store;
    ca.clearValue = { 0.15, 0.183, 0.2, 1.0 };
    WGPURenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments     = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _uniformBG, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, _vertexBuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, _indexBuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    for (const auto& batch : batches) {
        if (batch.idxCount == 0) continue;
        WGPUBindGroup texBG = _getOrCreateTexBG(batch.texID);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBG, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, batch.idxCount, 1, batch.idxOffset, 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(_queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

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
    for (uint32_t s = 0; s < UNIFORM_SLOT_COUNT; ++s) {
        if (_vertexBufs[s]) wgpuBufferRelease(_vertexBufs[s]);
        if (_indexBufs[s])  wgpuBufferRelease(_indexBufs[s]);
    }
    if (_whiteTex)   wgpuTextureRelease(_whiteTex);
    if (_sampler)    wgpuSamplerRelease(_sampler);
}

void GPUCanvasPass::_init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format) {
    _device = device;
    _queue  = queue;

    // ── Buffers (one geometry pair per pass variant — see header) ──────────
    for (uint32_t s = 0; s < UNIFORM_SLOT_COUNT; ++s) {
        {
            WGPUBufferDescriptor d{};
            d.size  = (uint64_t)MAX_VERTS * FLOATS_PER_VERT * sizeof(float);
            d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            _vertexBufs[s] = wgpuDeviceCreateBuffer(device, &d);
        }
        {
            WGPUBufferDescriptor d{};
            d.size  = (uint64_t)MAX_INDICES * sizeof(uint32_t);
            d.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            _indexBufs[s] = wgpuDeviceCreateBuffer(device, &d);
        }
    }
    {
        // One 256-byte slot per pass variant (world / screen / UI), selected
        // via dynamic offset. A single shared slot is WRONG: WriteBuffer
        // takes effect at submit, not at record time, so when WorldCanvasPass,
        // CanvasPass and UIPass each wrote their viewProj into the same slot
        // during one frame, the LAST write won for all three — world sprites
        // ended up projected with the screen-space ortho matrix.
        WGPUBufferDescriptor d{};
        d.size  = UNIFORM_SLOT_COUNT * UNIFORM_SLOT_STRIDE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }

    // ── Sampler ────────────────────────────────────────────────────────────
    // gpuSamplerDesc defaults (Nearest, ClampToEdge) are exactly what the
    // pixel-art canvas wants.
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
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

    // ── First pipeline: blend on, depth ignored (2D screenspace canvas) ────
    // sceneRT's render pass always carries a Depth32Float attachment, so the
    // pipeline must declare a matching depthStencil state even though the
    // canvas neither tests nor writes depth — write=false + Always is the
    // "depth-transparent" configuration that keeps attachment states
    // compatible (Dawn rejects the pipeline in the pass otherwise).
    // Vertex layout: pos(2f) uv(2f) color(4f) flags(2f) = 10 floats = 40 B.
    const std::vector<GpuVertexAttr> quadAttrs = {
        {WGPUVertexFormat_Float32x3,  0, 0}, // pos (xyz — see FLOATS_PER_VERT)
        {WGPUVertexFormat_Float32x2, 12, 1}, // uv
        {WGPUVertexFormat_Float32x4, 20, 2}, // color
        {WGPUVertexFormat_Float32x2, 36, 3}, // texIdx(unused), flags
    };
    auto p = GpuPipelineBuilder(device)
        .wgsl(QUAD_WGSL)
        .bgl({ gpuDynUniform(0, wgsl_stage::vert, 64) })
        .bgl({ gpuTexture(0), gpuSampler(1) })
        .vertex((uint64_t)FLOATS_PER_VERT * sizeof(float), quadAttrs)
        .colorFormat(format).blend()
        .depth(false, WGPUCompareFunction_Always)
        .build();
    _pipeline   = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL     = p.bgl(1);

    // ── Uniform bind group (must come after build so _uniformBGL is valid) ──
    // size = one slot's binding size (64B mat4); the slot is picked per draw
    // via dynamic offset in Render().
    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 64).build();

    // ── Depth-tested variant (WorldCanvasPass): read-only LessEqual ────────
    // Borrows the BGLs created above so bind groups stay compatible.
    _pipelineDepthTest = GpuPipelineBuilder(device)
        .wgsl(QUAD_WGSL)
        .bgl(_uniformBGL)
        .bgl(_texBGL)
        .vertex((uint64_t)FLOATS_PER_VERT * sizeof(float), quadAttrs)
        .colorFormat(format).blend()
        .depth(false, WGPUCompareFunction_LessEqual)
        .build().pipeline;

    // ── Swapchain-format variant (UIPass): no depth, blend on, but targets ──
    // GfxFactory::GetSwapchainFormat() instead of sceneRT's HDR format, since
    // UIPass runs after PostProcessPass has already resolved sceneRT to the
    // swapchain. Borrows the same BGLs for bind-group compatibility.
    _pipelineSwapchain = GpuPipelineBuilder(device)
        .wgsl(QUAD_WGSL)
        .bgl(_uniformBGL)
        .bgl(_texBGL)
        .vertex((uint64_t)FLOATS_PER_VERT * sizeof(float), quadAttrs)
        .colorFormat(GfxFactory::GetSwapchainFormat()).blend()
        .build().pipeline;

    _verts.reserve((size_t)MAX_VERTS * FLOATS_PER_VERT);
    _indices.reserve((size_t)MAX_INDICES);
}

WGPUBindGroup GPUCanvasPass::_getOrCreateTexBG(uint32_t texID) {
    auto it = _texBGCache.find(texID);
    if (it != _texBGCache.end()) return it->second;

    WGPUTexture rawTex = (texID == 0) ? _whiteTex : GfxFactory::GetWGPUTexture(texID);
    if (!rawTex) rawTex = _whiteTex;

    WGPUBindGroup bg = GpuBindGroupBuilder(_device, _texBGL)
        .texture(0, rawTex)
        .sampler(1, _sampler)
        .build();
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
            _verts.push_back(bv.position.z);
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

    // ── Upload into this variant's own slot/buffers ────────────────────────
    // wgpuQueueWriteBuffer applies at submit time (not record time), so each
    // of the three per-frame invocations must write disjoint destinations —
    // otherwise the last writer's matrix AND geometry replace everyone's.
    const uint32_t slot       = toSwapchain ? 2u : depthTest ? 1u : 0u;
    const uint32_t uniformOff = slot * UNIFORM_SLOT_STRIDE;
    wgpuQueueWriteBuffer(_queue, _uniformBuf, uniformOff,
                         glm::value_ptr(viewProj), 64);
    wgpuQueueWriteBuffer(_queue, _vertexBufs[slot], 0,
                         _verts.data(), _verts.size() * sizeof(float));
    wgpuQueueWriteBuffer(_queue, _indexBufs[slot], 0,
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
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _uniformBG, 1, &uniformOff);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, _vertexBufs[slot], 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, _indexBufs[slot], WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    for (const auto& batch : batches) {
        if (batch.idxCount == 0) continue;
        WGPUBindGroup texBG = _getOrCreateTexBG(batch.texID);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBG, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, batch.idxCount, 1, batch.idxOffset, 0, 0);
    }
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

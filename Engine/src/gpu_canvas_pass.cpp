#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_canvas_pass.hpp"
#include "gfx_factory.hpp"
#include "gpu_pipeline.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

GPUCanvasPass::~GPUCanvasPass() {
    for (auto& [id, entry] : _texBGCache)
        wgpuBindGroupRelease(entry.bg);
    if (_uniformBG) wgpuBindGroupRelease(_uniformBG);
    if (_uniformBGL) wgpuBindGroupLayoutRelease(_uniformBGL);
    if (_texBGL) wgpuBindGroupLayoutRelease(_texBGL);
    for (auto& [k, p] : _pipelines)
        if (p) wgpuRenderPipelineRelease(p);
    if (_uniformBuf) wgpuBufferRelease(_uniformBuf);
    for (uint32_t s = 0; s < UNIFORM_SLOT_COUNT; ++s) {
        if (_vertexBufs[s]) wgpuBufferRelease(_vertexBufs[s]);
        if (_indexBufs[s]) wgpuBufferRelease(_indexBufs[s]);
    }
    if (_whiteTex) wgpuTextureRelease(_whiteTex);
    if (_samplerLinear) wgpuSamplerRelease(_samplerLinear);
    if (_samplerNearest) wgpuSamplerRelease(_samplerNearest);
}

void GPUCanvasPass::_init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format, uint32_t sceneSampleCount) {
    _device = device;
    _queue = queue;
    _sceneFormat = format;
    _sceneSampleCount = sceneSampleCount;

    // ── Buffers (one geometry pair per pass variant — see header) ──────────
    for (uint32_t s = 0; s < UNIFORM_SLOT_COUNT; ++s) {
        {
            WGPUBufferDescriptor d{};
            d.size = static_cast<uint64_t>(MAX_VERTS) * FLOATS_PER_VERT * sizeof(float);
            d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            _vertexBufs[s] = wgpuDeviceCreateBuffer(device, &d);
        }
        {
            WGPUBufferDescriptor d{};
            d.size = static_cast<uint64_t>(MAX_INDICES) * sizeof(uint32_t);
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
        d.size = UNIFORM_SLOT_COUNT * UNIFORM_SLOT_STRIDE;
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        _uniformBuf = wgpuDeviceCreateBuffer(device, &d);
    }

    // ── Samplers ─────────────────────────────────────────────────────────────
    // One per filter mode; _getOrCreateTexBG picks per texture based on the
    // filter it was uploaded with, mirroring GL's per-texture glTexParameteri.
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _samplerLinear = wgpuDeviceCreateSampler(device, &d);
    }
    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();// defaults: Nearest
        _samplerNearest = wgpuDeviceCreateSampler(device, &d);
    }

    // ── White 1×1 texture ──────────────────────────────────────────────────
    {
        WGPUTextureDescriptor d{};
        d.size = { 1, 1, 1 };
        d.format = WGPUTextureFormat_RGBA8Unorm;
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension = WGPUTextureDimension_2D;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        _whiteTex = wgpuDeviceCreateTexture(device, &d);
        const uint8_t white[4] = { 255, 255, 255, 255 };
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _whiteTex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, white, 4, &layout, &extent);
    }

    // ── First pipeline: Canvas variant, Alpha blend ────────────────────────
    // Built here up front to populate _uniformBGL and _texBGL. Other
    // (variant, blend) combinations are built on demand by
    // _getOrCreatePipeline() using these BGLs so all bind groups stay
    // compatible. sceneRT's render pass always carries a Depth32Float
    // attachment, so pipelines targeting it must declare a matching
    // depthStencil state even when they don't test or write depth —
    // write=false + Always is the "depth-transparent" configuration that
    // keeps attachment states compatible (Dawn rejects otherwise).
    // Vertex layout: pos(3f) uv(2f) color(4f) flags(2f) = 11 floats = 44 B.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(QUAD_WGSL)
                 .bgl({ gpuDynUniform(0, wgsl_stage::vert, 64) })
                 .bgl({ gpuTexture(0), gpuSampler(1) })
                 .vertex(static_cast<uint64_t>(FLOATS_PER_VERT) * sizeof(float), _quadAttrs())
                 .colorFormat(format)
                 .blend(GpuPipelineBuilder::Blend::Alpha)
                 .depth(false, WGPUCompareFunction_Always)
                 .multisample(sceneSampleCount)
                 .build();
    _pipelines[{ Variant::Canvas, _toRenderState(BlendMode::Alpha).Hash() }] = p.pipeline;
    _uniformBGL = p.bgl(0);
    _texBGL = p.bgl(1);

    // ── Uniform bind group (must come after build so _uniformBGL is valid) ──
    // size = one slot's binding size (64B mat4); the slot is picked per draw
    // via dynamic offset in Render().
    _uniformBG = GpuBindGroupBuilder(device, _uniformBGL).buffer(0, _uniformBuf, 64).build();

    _verts.reserve(static_cast<size_t>(MAX_VERTS) * FLOATS_PER_VERT);
    _indices.reserve(static_cast<size_t>(MAX_INDICES));
}

const std::vector<GpuVertexAttr>& GPUCanvasPass::_quadAttrs() {
    static const std::vector<GpuVertexAttr> attrs = {
        { WGPUVertexFormat_Float32x3, 0, 0 },// pos (xyz — see FLOATS_PER_VERT)
        { WGPUVertexFormat_Float32x2, 12, 1 },// uv
        { WGPUVertexFormat_Float32x4, 20, 2 },// color
        { WGPUVertexFormat_Float32x2, 36, 3 },// texIdx(unused), flags
    };
    return attrs;
}

WGPURenderPipeline GPUCanvasPass::_getOrCreatePipeline(Variant v, const RenderState& rs) {
    const size_t rsHash = rs.Hash();
    auto it = _pipelines.find({ v, rsHash });
    if (it != _pipelines.end()) return it->second;

    // Match _init's Canvas/Alpha pipeline in every axis except the varying two.
    auto builder = GpuPipelineBuilder(_device)
                       .wgsl(QUAD_WGSL)
                       .bgl(_uniformBGL)
                       .bgl(_texBGL)
                       .vertex((uint64_t)FLOATS_PER_VERT * sizeof(float), _quadAttrs())
                       .blend(_mapBlend(rs.blend));

    switch (v) {
    case Variant::Canvas:
        // Screen-space canvas: same target as WorldCanvas below (sceneRT), but
        // depth-transparent so it renders on top of 3D without any test.
        builder.colorFormat(_sceneFormat).depth(false, WGPUCompareFunction_Always).multisample(_sceneSampleCount);
        break;
    case Variant::WorldCanvas:
        // World-space canvas: depth-tested read-only against sceneRT's depth
        // so 2D sprites are occluded by (but don't occlude) 3D geometry.
        builder.colorFormat(_sceneFormat).depth(false, WGPUCompareFunction_LessEqual).multisample(_sceneSampleCount);
        break;
    case Variant::UI:
        // UIPass runs after PostProcessPass has resolved sceneRT to the
        // swapchain — different color format, no depth, single sample.
        builder.colorFormat(GfxFactory::GetSwapchainFormat());
        break;
    }

    WGPURenderPipeline pipe = builder.build().pipeline;
    _pipelines[{ v, rsHash }] = pipe;
    return pipe;
}

RenderState GPUCanvasPass::_toRenderState(BlendMode m) {
    RenderState rs;
    rs.blend = m;
    // Canvas draws don't cull or test depth in the material sense — the
    // variant-specific depth/cull state lives in the pipeline builder switch
    // above, so leave the RenderState fields at their defaults. Only `blend`
    // varies with the per-command state.
    return rs;
}

GpuPipelineBuilder::Blend GPUCanvasPass::_mapBlend(BlendMode m) {
    switch (m) {
    case BlendMode::None:
        return GpuPipelineBuilder::Blend::None;
    case BlendMode::Alpha:
        return GpuPipelineBuilder::Blend::Alpha;
    case BlendMode::Additive:
        return GpuPipelineBuilder::Blend::Additive;
    case BlendMode::Multiply:
        return GpuPipelineBuilder::Blend::Multiply;
    case BlendMode::Screen:
        return GpuPipelineBuilder::Blend::Screen;
    case BlendMode::Premultiplied:
        return GpuPipelineBuilder::Blend::Premultiplied;
    }
    return GpuPipelineBuilder::Blend::Alpha;
}

WGPUBindGroup GPUCanvasPass::_getOrCreateTexBG(uint32_t texID) {
    // The synthetic ID is stable across GfxFactory::UpdateTexture2D resizes,
    // but the WGPUTexture underneath is NOT (WebGPU texture storage is
    // immutable — resize recreates it). Re-resolve the texture every call and
    // rebuild the bind group when it changed, or we'd sample a dead texture
    // (VideoPlayer's 1x1 placeholder → first-frame resize).
    WGPUTexture rawTex = (texID == 0) ? _whiteTex : GfxFactory::GetWGPUTexture(texID);
    if (!rawTex) rawTex = _whiteTex;

    auto it = _texBGCache.find(texID);
    if (it != _texBGCache.end()) {
        if (it->second.tex == rawTex) return it->second.bg;
        wgpuBindGroupRelease(it->second.bg);
        _texBGCache.erase(it);
    }

    // Filter per texture, like GL's per-object glTexParameteri (texID 0 is the
    // 1x1 white sentinel — filter is irrelevant, use linear).
    WGPUSampler samp = (texID != 0 && GfxFactory::GetTextureFilter(texID) == TextureFilter::Nearest) ? _samplerNearest
                                                                                                     : _samplerLinear;
    WGPUBindGroup bg = GpuBindGroupBuilder(_device, _texBGL).texture(0, rawTex).sampler(1, samp).build();
    _texBGCache[texID] = { bg, rawTex };
    return bg;
}

void GPUCanvasPass::Render(
    CommandEncoder* enc,
    const glm::mat4& viewProj,
    const std::vector<BatchDrawCommand>& commands,
    bool depthTest,
    bool toSwapchain,
    uint32_t sceneSampleCount
) {
    // Lazy init: wait until GfxFactory has a live device
    if (!_uniformBGL) {
        WGPUDevice dev = GfxFactory::GetWebGPUDevice();
        WGPUQueue q = GfxFactory::GetWebGPUQueue();
        if (!dev) return;
        _init(dev, q, WGPUTextureFormat_RGBA16Float, sceneSampleCount);// sceneRT's HDR format
    }

    // GL-convention projections put NDC z in [-1,1]; WebGPU clips outside
    // [0,1]. A 2D-preset ortho camera (near=-100, far=1000) lands z=0 sprites
    // at ndc z≈-0.82 — visible under GL, clipped entirely under WebGPU. Remap
    // z for the variants that don't depth-test; the depth-tested variant must
    // stay in the same convention as the 3D passes that wrote sceneRT's depth.
    glm::mat4 vp = depthTest ? viewProj : GpuProjectionZ01() * viewProj;

    if (commands.empty()) return;

    // ── Build CPU vertex/index staging and per-(texture, blend) batch list ─
    // Batches split on either axis change so pipeline / bind-group binds can
    // stay contiguous during the record loop.
    struct DrawBatch {
        uint32_t texID;
        BlendMode blend;
        uint32_t idxOffset;
        uint32_t idxCount;
    };
    std::vector<DrawBatch> batches;

    _verts.clear();
    _indices.clear();

    uint32_t curTex = UINT32_MAX;
    BlendMode curBlend = BlendMode::None;
    bool haveBatch = false;
    int vertOff = 0;
    uint32_t idxOff = 0;

    for (const auto& cmd : commands) {
        if (!haveBatch || cmd.textureID != curTex || cmd.blendMode != curBlend) {
            curTex = cmd.textureID;
            curBlend = cmd.blendMode;
            haveBatch = true;
            batches.push_back({ curTex, curBlend, idxOff, 0u });
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
            _verts.push_back(0.0f);// texIdx slot (unused — single-tex batching)
            _verts.push_back(flags);
        }
        for (uint32_t idx : cmd.indices)
            _indices.push_back(idx + static_cast<uint32_t>(vertOff));

        batches.back().idxCount += static_cast<uint32_t>(cmd.indices.size());
        vertOff += static_cast<int>(cmd.vertices.size());
        idxOff += static_cast<uint32_t>(cmd.indices.size());
    }

    // ── Upload into this variant's own slot/buffers ────────────────────────
    // wgpuQueueWriteBuffer applies at submit time (not record time), so each
    // of the three per-frame invocations must write disjoint destinations —
    // otherwise the last writer's matrix AND geometry replace everyone's.
    const uint32_t slot = toSwapchain ? 2u : depthTest ? 1u : 0u;
    const uint32_t uniformOff = slot * UNIFORM_SLOT_STRIDE;
    wgpuQueueWriteBuffer(_queue, _uniformBuf, uniformOff, glm::value_ptr(vp), 64);
    wgpuQueueWriteBuffer(_queue, _vertexBufs[slot], 0, _verts.data(), _verts.size() * sizeof(float));
    wgpuQueueWriteBuffer(_queue, _indexBufs[slot], 0, _indices.data(), _indices.size() * sizeof(uint32_t));

    // ── Record into the render pass already opened by the caller ──────────
    // (caller must have called sceneRT->Begin(enc) before this and will call
    // End() afterward — we do not own the pass or command-buffer lifecycle.)
    auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
    WGPURenderPassEncoder pass = gpuEnc->pass;
    if (!pass) return;

    const Variant variant = toSwapchain ? Variant::UI : depthTest ? Variant::WorldCanvas : Variant::Canvas;

    wgpuRenderPassEncoderSetBindGroup(pass, 0, _uniformBG, 1, &uniformOff);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, _vertexBufs[slot], 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, _indexBufs[slot], WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    WGPURenderPipeline curPipeline = nullptr;
    for (const auto& batch : batches) {
        if (batch.idxCount == 0) continue;
        WGPURenderPipeline pipe = _getOrCreatePipeline(variant, _toRenderState(batch.blend));
        if (pipe != curPipeline) {
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            curPipeline = pipe;
        }
        WGPUBindGroup texBG = _getOrCreateTexBG(batch.texID);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, texBG, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, batch.idxCount, 1, batch.idxOffset, 0, 0);
    }
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

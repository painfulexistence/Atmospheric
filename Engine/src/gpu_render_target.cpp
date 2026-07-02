#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_render_target.hpp"

GPURenderTarget::GPURenderTarget(WGPUDevice device, const RenderTarget::Props& props)
    : _device(device),
      _width(props.width),
      _height(props.height),
      _withDepth(props.withDepth),
      _hdr(props.hdr) {
    Create();
}

GPURenderTarget::~GPURenderTarget() {
    Destroy();
}

void GPURenderTarget::Create() {
    WGPUTextureDescriptor colorDesc{};
    // CopySrc: BloomPass snapshots sceneRT's color texture into a separate
    // texture before compositing bloom back in, since a texture cannot be
    // bound as both a render attachment and a sampled texture in the same pass.
    colorDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    colorDesc.dimension     = WGPUTextureDimension_2D;
    colorDesc.size          = { (uint32_t)_width, (uint32_t)_height, 1 };
    colorDesc.format        = _hdr ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm;
    colorDesc.mipLevelCount = 1;
    colorDesc.sampleCount   = 1;
    _colorTexture = wgpuDeviceCreateTexture(_device, &colorDesc);

    if (_withDepth) {
        WGPUTextureDescriptor depthDesc{};
        depthDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        depthDesc.dimension     = WGPUTextureDimension_2D;
        depthDesc.size          = { (uint32_t)_width, (uint32_t)_height, 1 };
        depthDesc.format        = WGPUTextureFormat_Depth32Float;
        depthDesc.mipLevelCount = 1;
        depthDesc.sampleCount   = 1;
        _depthTexture = wgpuDeviceCreateTexture(_device, &depthDesc);
    }
}

void GPURenderTarget::Destroy() {
    if (_activePass) {
        wgpuRenderPassEncoderEnd(_activePass);
        wgpuRenderPassEncoderRelease(_activePass);
        _activePass = nullptr;
    }
    if (_activeEnc) {
        _activeEnc->pass = nullptr;
        _activeEnc = nullptr;
    }
    if (_colorView)   { wgpuTextureViewRelease(_colorView);   _colorView   = nullptr; }
    if (_depthView)   { wgpuTextureViewRelease(_depthView);   _depthView   = nullptr; }
    if (_colorTexture){ wgpuTextureRelease(_colorTexture);    _colorTexture = nullptr; }
    if (_depthTexture){ wgpuTextureRelease(_depthTexture);    _depthTexture = nullptr; }
}

void GPURenderTarget::Begin(CommandEncoder* enc) {
    auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);

    _colorView = wgpuTextureCreateView(_colorTexture, nullptr);

    WGPURenderPassColorAttachment colorAttach{};
    // Zero-init leaves depthSlice = 0, but for non-3D attachments Dawn
    // requires WGPU_DEPTH_SLICE_UNDEFINED (newer Chrome validates this).
    colorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttach.view       = _colorView;
    colorAttach.loadOp     = _clearPending ? WGPULoadOp_Clear : WGPULoadOp_Load;
    colorAttach.storeOp    = WGPUStoreOp_Store;
    colorAttach.clearValue = { _clearColor.r, _clearColor.g, _clearColor.b, _clearColor.a };

    WGPURenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;

    WGPURenderPassDepthStencilAttachment depthAttach{};
    if (_withDepth && _depthTexture) {
        _depthView = wgpuTextureCreateView(_depthTexture, nullptr);
        depthAttach.view            = _depthView;
        depthAttach.depthLoadOp     = _clearPending ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttach.depthStoreOp    = WGPUStoreOp_Store;
        depthAttach.depthClearValue = 1.0f;
        passDesc.depthStencilAttachment = &depthAttach;
    }

    _clearPending = false;

    _activePass  = wgpuCommandEncoderBeginRenderPass(gpuEnc->encoder, &passDesc);
    gpuEnc->pass = _activePass;
    _activeEnc   = gpuEnc;
}

void GPURenderTarget::End() {
    if (_activePass) {
        wgpuRenderPassEncoderEnd(_activePass);
        wgpuRenderPassEncoderRelease(_activePass);
        _activePass = nullptr;
    }
    if (_activeEnc) {
        _activeEnc->pass = nullptr;
        _activeEnc = nullptr;
    }
    if (_colorView) { wgpuTextureViewRelease(_colorView); _colorView = nullptr; }
    if (_depthView) { wgpuTextureViewRelease(_depthView); _depthView = nullptr; }
}

void GPURenderTarget::Clear(const glm::vec4& color) {
    _clearColor   = color;
    _clearPending = true;
}

uint32_t GPURenderTarget::GetTextureID() const {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_colorTexture));
}

uint32_t GPURenderTarget::GetDepthTextureID() const {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_depthTexture));
}

void GPURenderTarget::Resize(int width, int height) {
    if (width == _width && height == _height) return;
    _width  = width;
    _height = height;
    Destroy();
    Create();
}
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

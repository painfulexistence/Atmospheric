#pragma once
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "render_target.hpp"
#include "command_encoder.hpp"
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

// WebGPU implementation of RenderTarget (Emscripten + AE_USE_WEBGPU).
//
// Begin(enc):  creates texture views, opens a WGPURenderPassEncoder, and
//              sets enc->pass so subsequent Draw() calls can bind buffers.
// End():       ends the render pass and releases transient views.
// Clear():     stores the clear colour; applied via loadOp=Clear on next Begin().
//
// GetTextureID() returns the low 32 bits of the WGPUTexture pointer for
// interface compatibility. Use GetNativeTexture() for shader binding.
class GPURenderTarget : public RenderTarget {
public:
    GPURenderTarget(WGPUDevice device, const RenderTarget::Props& props);
    ~GPURenderTarget() override;

    GPURenderTarget(const GPURenderTarget&) = delete;
    GPURenderTarget& operator=(const GPURenderTarget&) = delete;

    void Begin(CommandEncoder* enc = nullptr) override;
    void End() override;
    void Clear(const glm::vec4& color = glm::vec4(0.0f)) override;

    uint32_t GetTextureID() const override;
    uint32_t GetDepthTextureID() const override;

    int GetWidth() const override { return _width; }
    int GetHeight() const override { return _height; }
    glm::vec2 GetSize() const override { return { static_cast<float>(_width), static_cast<float>(_height) }; }
    int GetNumSamples() const override { return _samples; }

    bool IsValid() const override { return _colorTexture != nullptr; }
    void Resize(int width, int height) override;

    // Always single-sampled: with MSAA on, this is the resolve target that
    // every pass's End() resolves into — safe to sample/copy at any time.
    WGPUTexture GetNativeTexture() const { return _colorTexture; }
    WGPUTexture GetNativeDepthTexture() const { return _depthTexture; }

private:
    void Create();
    void Destroy();

    WGPUDevice            _device       = nullptr;
    WGPUTexture           _colorTexture = nullptr; // single-sample (resolve target under MSAA)
    WGPUTexture           _msaaTexture  = nullptr; // multisampled color, only when _samples > 1
    WGPUTexture           _depthTexture = nullptr; // sampleCount matches _samples
    WGPUTextureView       _colorView    = nullptr;
    WGPUTextureView       _msaaView     = nullptr;
    WGPUTextureView       _depthView    = nullptr;
    WGPURenderPassEncoder _activePass   = nullptr;
    // Encoder whose ->pass we set in Begin(); kept so End() can null it out
    // again. Without this, enc->pass dangles after End() releases the pass
    // encoder, and the `if (!pass)` guards downstream can't catch it.
    GPUCommandEncoder*    _activeEnc    = nullptr;

    int  _width     = 0;
    int  _height    = 0;
    int  _samples   = 1;
    bool _withDepth = false;
    bool _hdr       = false;
    glm::vec4 _clearColor = glm::vec4(0.0f);
    // Set by Clear(), consumed (and reset to false) by the next Begin() so
    // only that one Begin() uses loadOp=Clear; later Begin() calls within the
    // same frame load the existing contents instead of erasing them.
    bool _clearPending = false;
};
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

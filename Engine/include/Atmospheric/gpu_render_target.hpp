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
    glm::vec2 GetSize() const override { return { (float)_width, (float)_height }; }

    bool IsValid() const override { return _colorTexture != nullptr; }
    void Resize(int width, int height) override;

    WGPUTexture GetNativeTexture() const { return _colorTexture; }
    WGPUTexture GetNativeDepthTexture() const { return _depthTexture; }

private:
    void Create();
    void Destroy();

    WGPUDevice            _device       = nullptr;
    WGPUTexture           _colorTexture = nullptr;
    WGPUTexture           _depthTexture = nullptr;
    WGPUTextureView       _colorView    = nullptr;
    WGPUTextureView       _depthView    = nullptr;
    WGPURenderPassEncoder _activePass   = nullptr;

    int  _width     = 0;
    int  _height    = 0;
    bool _withDepth = false;
    bool _hdr       = false;
    glm::vec4 _clearColor = glm::vec4(0.0f);
};
#endif // AE_USE_WEBGPU && __EMSCRIPTEN__

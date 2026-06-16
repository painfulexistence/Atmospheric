#pragma once
#include "command_encoder.hpp"
#include <cstdint>
#include <glm/glm.hpp>

// Abstract offscreen render target (framebuffer + color/depth attachments).
class RenderTarget {
public:
    struct Props {
        int width = 256;
        int height = 256;
        bool withDepth = false;
        bool withStencil = false;
        bool hdr = false;      // RGBA16F instead of RGBA8
        bool filtered = true;  // Linear vs Nearest sampling
        int numSamples = 1;    // >1 enables MSAA (GL_TEXTURE_2D_MULTISAMPLE on desktop)
    };

    virtual ~RenderTarget() = default;

    virtual void Begin(CommandEncoder* enc = nullptr) = 0;
    virtual void End() = 0;
    virtual void Clear(const glm::vec4& color = glm::vec4(0.0f)) = 0;

    // Color attachment handle (GLuint on GL, low bits of WGPUTexture* on WebGPU).
    virtual uint32_t GetTextureID() const = 0;
    // Depth attachment handle, or 0 if not created with a depth attachment.
    virtual uint32_t GetDepthTextureID() const = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual glm::vec2 GetSize() const = 0;

    virtual bool IsValid() const = 0;
    virtual void Resize(int width, int height) = 0;
};

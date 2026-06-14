#pragma once
#include "render_target.hpp"
#include "globals.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// OpenGL implementation of RenderTarget.
// Wraps an FBO with a color texture attachment and optional depth/stencil attachment.
class GLRenderTarget : public RenderTarget {
public:
    using Props = RenderTarget::Props;

    GLRenderTarget(int width, int height, bool withDepth = false);
    explicit GLRenderTarget(const Props& props);
    ~GLRenderTarget() override;

    GLRenderTarget(const GLRenderTarget&) = delete;
    GLRenderTarget& operator=(const GLRenderTarget&) = delete;
    GLRenderTarget(GLRenderTarget&& other) noexcept;
    GLRenderTarget& operator=(GLRenderTarget&& other) noexcept;

    // enc is unused for GL.
    void Begin(CommandEncoder* enc = nullptr) override;
    void End() override;
    void Clear(const glm::vec4& color = glm::vec4(0.0f)) override;

    uint32_t GetTextureID() const override { return _colorTexture; }
    uint32_t GetDepthTextureID() const override { return _depthTexture; }

    int GetWidth() const override { return _width; }
    int GetHeight() const override { return _height; }
    glm::vec2 GetSize() const override { return glm::vec2(_width, _height); }

    bool IsValid() const override { return _fbo != 0 && _colorTexture != 0; }

    void Resize(int width, int height) override;

    GLuint GetNativeFBOID() const { return _fbo; }

    // 2D projection helpers
    glm::mat4 GetProjectionMatrix() const {
        return glm::ortho(0.0f, (float)_width, (float)_height, 0.0f, -1.0f, 1.0f);
    }
    glm::mat4 GetProjectionMatrixFlipped() const {
        return glm::ortho(0.0f, (float)_width, 0.0f, (float)_height, -1.0f, 1.0f);
    }

private:
    void Create();
    void Destroy();

    GLuint _fbo              = 0;
    GLuint _colorTexture     = 0;
    GLuint _depthTexture     = 0;
    GLuint _depthStencilRBO  = 0;

    int  _width       = 0;
    int  _height      = 0;
    bool _withDepth   = false;
    bool _withStencil = false;
    bool _hdr         = false;
    bool _filtered    = true;
    int  _numSamples  = 1;
    bool _multisample = false;

    GLint _prevFBO          = 0;
    GLint _prevViewport[4]  = {0, 0, 0, 0};
};

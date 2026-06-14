#include "gl_render_target.hpp"
#include "console.hpp"

GLRenderTarget::GLRenderTarget(int width, int height, bool withDepth)
    : _width(width), _height(height), _withDepth(withDepth) {
    Create();
}

GLRenderTarget::GLRenderTarget(const Props& props)
    : _width(props.width),
      _height(props.height),
      _withDepth(props.withDepth),
      _withStencil(props.withStencil),
      _hdr(props.hdr),
      _filtered(props.filtered),
      _numSamples(props.numSamples),
      _multisample(props.numSamples > 1) {
    Create();
}

GLRenderTarget::~GLRenderTarget() {
    Destroy();
}

GLRenderTarget::GLRenderTarget(GLRenderTarget&& other) noexcept
    : _fbo(other._fbo),
      _colorTexture(other._colorTexture),
      _depthTexture(other._depthTexture),
      _depthStencilRBO(other._depthStencilRBO),
      _width(other._width),
      _height(other._height),
      _withDepth(other._withDepth),
      _withStencil(other._withStencil),
      _hdr(other._hdr),
      _filtered(other._filtered),
      _numSamples(other._numSamples),
      _multisample(other._multisample) {
    other._fbo             = 0;
    other._colorTexture    = 0;
    other._depthTexture    = 0;
    other._depthStencilRBO = 0;
}

GLRenderTarget& GLRenderTarget::operator=(GLRenderTarget&& other) noexcept {
    if (this != &other) {
        Destroy();
        _fbo             = other._fbo;
        _colorTexture    = other._colorTexture;
        _depthTexture    = other._depthTexture;
        _depthStencilRBO = other._depthStencilRBO;
        _width           = other._width;
        _height          = other._height;
        _withDepth       = other._withDepth;
        _withStencil     = other._withStencil;
        _hdr             = other._hdr;
        _filtered        = other._filtered;
        _numSamples      = other._numSamples;
        _multisample     = other._multisample;
        other._fbo             = 0;
        other._colorTexture    = 0;
        other._depthTexture    = 0;
        other._depthStencilRBO = 0;
    }
    return *this;
}

void GLRenderTarget::Create() {
    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

    glGenTextures(1, &_colorTexture);

#ifndef __EMSCRIPTEN__
    if (_multisample) {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _colorTexture);
        GLenum internalFormat = _hdr ? GL_RGBA16F : GL_RGBA8;
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, _numSamples, internalFormat, _width, _height, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, _colorTexture, 0);

        if (_withDepth) {
            glGenTextures(1, &_depthTexture);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, _depthTexture);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, _numSamples, GL_DEPTH_COMPONENT32F, _width, _height, GL_TRUE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, _depthTexture, 0);
        }
    } else {
#else
    // TODO: Implement WebGL-compatible offscreen MSAA.
    // WebGL 2.0 does not support GL_TEXTURE_2D_MULTISAMPLE. To support offscreen MSAA with post-processing 
    // on Web, we must allocate multisampled Renderbuffers using glRenderbufferStorageMultisample 
    // and bind them to the framebuffer, then resolve (blit) to a single-sampled texture FBO.
    if (false) {
    } else {
#endif
        glBindTexture(GL_TEXTURE_2D, _colorTexture);

        GLenum internalFormat = _hdr ? GL_RGBA16F : GL_RGBA8;
        GLenum type           = _hdr ? GL_FLOAT : GL_UNSIGNED_BYTE;
        GLenum format         = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, _width, _height, 0, format, type, nullptr);

        GLenum filterMode = _filtered ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorTexture, 0);

        if (_withDepth) {
            if (_withStencil) {
                glGenRenderbuffers(1, &_depthStencilRBO);
                glBindRenderbuffer(GL_RENDERBUFFER, _depthStencilRBO);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _width, _height);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, _depthStencilRBO);
            } else {
                glGenTextures(1, &_depthTexture);
                glBindTexture(GL_TEXTURE_2D, _depthTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, _width, _height, 0,
                             GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_TEXTURE_2D, _depthTexture, 0);
            }
        }
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Console::Get()->Error("GLRenderTarget: Framebuffer incomplete!");
        Destroy();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderTarget::Destroy() {
    if (_depthStencilRBO) { glDeleteRenderbuffers(1, &_depthStencilRBO); _depthStencilRBO = 0; }
    if (_depthTexture)    { glDeleteTextures(1, &_depthTexture);         _depthTexture    = 0; }
    if (_colorTexture)    { glDeleteTextures(1, &_colorTexture);         _colorTexture    = 0; }
    if (_fbo)             { glDeleteFramebuffers(1, &_fbo);              _fbo             = 0; }
}

void GLRenderTarget::Begin(CommandEncoder* /*enc*/) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_prevFBO);
    glGetIntegerv(GL_VIEWPORT, _prevViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glViewport(0, 0, _width, _height);
}

void GLRenderTarget::End() {
    glBindFramebuffer(GL_FRAMEBUFFER, _prevFBO);
    glViewport(_prevViewport[0], _prevViewport[1], _prevViewport[2], _prevViewport[3]);
}

void GLRenderTarget::Clear(const glm::vec4& color) {
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glClearColor(color.r, color.g, color.b, color.a);
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (_withDepth) {
        mask |= GL_DEPTH_BUFFER_BIT;
        if (_withStencil) mask |= GL_STENCIL_BUFFER_BIT;
    }
    glClear(mask);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}

void GLRenderTarget::Resize(int width, int height) {
    if (width == _width && height == _height) return;
    _width  = width;
    _height = height;
    Destroy();
    Create();
}

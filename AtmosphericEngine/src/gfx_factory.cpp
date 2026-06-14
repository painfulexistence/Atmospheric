#include "gfx_factory.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "console.hpp"
#include "globals.hpp"   // glad / GLES3

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#include <emscripten/html5_webgpu.h>
#include <emscripten/em_js.h>
#include "gpu_buffer.hpp"
#include "gpu_render_target.hpp"

EM_JS(int, ae_has_webgpu_device, (), {
    return Module.preinitializedWebGPUDevice ? 1 : 0;
});
#endif

// ── Static member definitions ────────────────────────────────────────────────
GfxBackend GfxFactory::_backend = GfxBackend::OpenGL;

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
WGPUDevice        GfxFactory::_wgpuDevice      = nullptr;
WGPUQueue         GfxFactory::_wgpuQueue        = nullptr;
WGPUSurface       GfxFactory::_surface          = nullptr;
WGPUTextureFormat GfxFactory::_swapchainFormat  = WGPUTextureFormat_BGRA8Unorm;
std::unordered_map<uint32_t, WGPUTexture> GfxFactory::_gpuTextures;
uint32_t          GfxFactory::_nextTexID        = 1;
#elif !defined(__EMSCRIPTEN__)
SDL_Window* GfxFactory::_sdlWindow = nullptr;
#endif

// ── Init ─────────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__

void GfxFactory::Init() {
#if defined(AE_USE_WEBGPU)
    if (Window::IsWebGPUAvailable()) {
        // wgpuInstanceRequestAdapter / wgpuAdapterRequestDevice in emsdk 4.x have a
        // mismatched ABI between webgpu.h (old 4-arg callback) and the precompiled
        // libemdawnwebgpu (new 3-arg Future API) — calling them causes a WASM trap.
        //
        // Instead, cmake/webgpu_preinit.js (injected via --pre-js) requests the
        // adapter+device in JS before WASM starts and stores the result in
        // Module.preinitializedWebGPUDevice.  We check that value via EM_ASM_INT
        // before calling emscripten_webgpu_get_device() because the latter asserts
        // (not returns null) when the value isn't set.
        int hasDevice = ae_has_webgpu_device();
        if (hasDevice) {
            WGPUDevice device = emscripten_webgpu_get_device();
            if (device) {
                Console::Get()->Info("[GfxFactory] WebGPU: got pre-initialised device.");
                _backend = GfxBackend::WebGPU;
                SetWebGPUDevice(device);
                return;
            }
        }
        Console::Get()->Warn("[GfxFactory] WebGPU available but device not pre-initialised "
                             "(Module.preinitializedWebGPUDevice not set). Falling back to WebGL 2.");
    } else {
        Console::Get()->Warn("[GfxFactory] WebGPU not available in browser. Falling back to WebGL 2.");
    }
#endif
    _backend = GfxBackend::OpenGL;
}

#if defined(AE_USE_WEBGPU)
void GfxFactory::SetWebGPUDevice(WGPUDevice device) {
    if (!device) {
        Console::Get()->Warn("[GfxFactory] SetWebGPUDevice: device is null. Falling back to WebGL 2.");
        _backend = GfxBackend::OpenGL;
        return;
    }
    Console::Get()->Info(fmt::format("[GfxFactory] SetWebGPUDevice: got device={}", (void*)device));
    _wgpuDevice = device;
    _wgpuQueue  = wgpuDeviceGetQueue(device);
    Console::Get()->Info(fmt::format("[GfxFactory] SetWebGPUDevice: queue={}", (void*)_wgpuQueue));

    // Create and configure the HTML canvas surface
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector    = "canvas";
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
    WGPUInstance inst = wgpuCreateInstance(nullptr);
    _surface = wgpuInstanceCreateSurface(inst, &surfDesc);
    wgpuInstanceRelease(inst);
    Console::Get()->Info(fmt::format("[GfxFactory] SetWebGPUDevice: surface={}", (void*)_surface));

    auto [w, h] = Window::Get()->GetFramebufferSize();
    Console::Get()->Info(fmt::format("[GfxFactory] SetWebGPUDevice: configuring surface {}x{}", w, h));
    _swapchainFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfiguration cfg{};
    cfg.device      = device;
    cfg.format      = _swapchainFormat;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = static_cast<uint32_t>(w);
    cfg.height      = static_cast<uint32_t>(h);
    cfg.presentMode = WGPUPresentMode_Fifo;
    cfg.alphaMode   = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(_surface, &cfg);

    Console::Get()->Info("[GfxFactory] WebGPU device ready. Surface configured.");
}

WGPUTextureView GfxFactory::GetCurrentSwapchainView() {
    if (!_surface) return nullptr;
    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(_surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_Success) return nullptr;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
    wgpuTextureRelease(st.texture);
    return view;
}

void GfxFactory::PresentSwapchain() {
    if (_surface) wgpuSurfacePresent(_surface);
}
#endif // AE_USE_WEBGPU

#else // native

void GfxFactory::Init(SDL_Window* sdlWindow) {
    _sdlWindow = sdlWindow;

    // TODO: Initialize Dawn WebGPU here once Dawn is integrated.
    _backend = GfxBackend::OpenGL;
}

#endif // __EMSCRIPTEN__

// ── Shutdown ─────────────────────────────────────────────────────────────────
void GfxFactory::Shutdown() {
#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    for (auto& [id, tex] : _gpuTextures) wgpuTextureRelease(tex);
    _gpuTextures.clear();
    if (_surface)    { wgpuSurfaceRelease(_surface);    _surface    = nullptr; }
    if (_wgpuDevice) { wgpuDeviceRelease(_wgpuDevice);  _wgpuDevice = nullptr; }
    _wgpuQueue = nullptr;
#elif !defined(__EMSCRIPTEN__)
    _sdlWindow = nullptr;
    // TODO: release Dawn instance / device / surface here.
#endif
}

// ── Cross-backend texture upload ─────────────────────────────────────────────
uint32_t GfxFactory::UploadTexture2D(const uint8_t* pixels, int w, int h) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) {
        uint32_t id = _nextTexID++;

        WGPUTextureDescriptor td{};
        td.size          = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        td.format        = WGPUTextureFormat_RGBA8Unorm;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension     = WGPUTextureDimension_2D;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(_wgpuDevice, &td);

        WGPUImageCopyTexture dst{};
        dst.texture = tex;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTextureDataLayout layout{};
        layout.bytesPerRow  = static_cast<uint32_t>(w) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuQueueWriteTexture(_wgpuQueue, &dst, pixels,
                               static_cast<size_t>(w) * h * 4, &layout, &extent);

        _gpuTextures[id] = tex;
        return id;
    }
#endif
    // OpenGL / WebGL path
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uint32_t>(texID);
}

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
WGPUTexture GfxFactory::GetWGPUTexture(uint32_t id) {
    auto it = _gpuTextures.find(id);
    return (it != _gpuTextures.end()) ? it->second : nullptr;
}
#endif

// ── Factory methods ──────────────────────────────────────────────────────────
std::unique_ptr<Buffer> GfxFactory::CreateBuffer() {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice)
        return std::make_unique<GPUBuffer>(_wgpuDevice, _wgpuQueue);
#elif !defined(__EMSCRIPTEN__)
    (void)_sdlWindow;
#endif
    return std::make_unique<GLBuffer>();
}

std::unique_ptr<RenderTarget> GfxFactory::CreateRenderTarget(
        const RenderTarget::Props& props) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice)
        return std::make_unique<GPURenderTarget>(_wgpuDevice, props);
#elif !defined(__EMSCRIPTEN__)
    (void)_sdlWindow;
#endif
    return std::make_unique<GLRenderTarget>(props);
}

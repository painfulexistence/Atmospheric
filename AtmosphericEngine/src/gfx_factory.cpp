#include "gfx_factory.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "console.hpp"
#include "globals.hpp"   // glad / GLES3

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#include "gpu_buffer.hpp"
#include "gpu_render_target.hpp"
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
    // ── Create instance ───────────────────────────────────────────────────────
    WGPUInstanceDescriptor instDesc{};
    WGPUInstance inst = wgpuCreateInstance(&instDesc);
    if (!inst) {
        Console::Get()->Warn("[GfxFactory] wgpuCreateInstance failed. Falling back to WebGL 2.");
        _backend = GfxBackend::OpenGL;
        return;
    }

    // ── Request adapter (Future API — requires -sASYNCIFY=1) ─────────────────
    WGPURequestAdapterOptions adapterOpts{};
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterCallbackInfo adapterCbInfo{};
    adapterCbInfo.mode     = WGPUCallbackMode_WaitAnyOnly;
    adapterCbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a,
                                WGPUStringView, void* ud1, void*) {
        if (status == WGPURequestAdapterStatus_Success)
            *static_cast<WGPUAdapter*>(ud1) = a;
    };
    adapterCbInfo.userdata1 = &adapter;

    WGPUFutureWaitInfo adapterWait{};
    adapterWait.future = wgpuInstanceRequestAdapter(inst, &adapterOpts, adapterCbInfo);
    wgpuInstanceWaitAny(inst, 1, &adapterWait, UINT64_MAX);

    if (!adapter) {
        Console::Get()->Warn("[GfxFactory] No WebGPU adapter. Falling back to WebGL 2.");
        wgpuInstanceRelease(inst);
        _backend = GfxBackend::OpenGL;
        return;
    }

    // ── Request device ────────────────────────────────────────────────────────
    WGPUDevice device = nullptr;
    WGPURequestDeviceCallbackInfo deviceCbInfo{};
    deviceCbInfo.mode     = WGPUCallbackMode_WaitAnyOnly;
    deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d,
                               WGPUStringView, void* ud1, void*) {
        if (status == WGPURequestDeviceStatus_Success)
            *static_cast<WGPUDevice*>(ud1) = d;
    };
    deviceCbInfo.userdata1 = &device;

    WGPUDeviceDescriptor deviceDesc{};
    WGPUFutureWaitInfo deviceWait{};
    deviceWait.future = wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCbInfo);
    wgpuInstanceWaitAny(inst, 1, &deviceWait, UINT64_MAX);
    wgpuAdapterRelease(adapter);

    if (!device) {
        Console::Get()->Warn("[GfxFactory] WebGPU device creation failed. Falling back to WebGL 2.");
        wgpuInstanceRelease(inst);
        _backend = GfxBackend::OpenGL;
        return;
    }

    Console::Get()->Info("[GfxFactory] WebGPU adapter and device acquired.");
    _backend    = GfxBackend::WebGPU;
    _wgpuDevice = device;
    _wgpuQueue  = wgpuDeviceGetQueue(device);

    // ── Create surface ────────────────────────────────────────────────────────
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector    = { "#canvas", WGPU_STRLEN };
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.nextInChain   = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
    _surface = wgpuInstanceCreateSurface(inst, &surfDesc);
    wgpuInstanceRelease(inst);

    if (!_surface) {
        Console::Get()->Warn("[GfxFactory] wgpuInstanceCreateSurface failed. Falling back to WebGL 2.");
        wgpuDeviceRelease(device);
        _wgpuDevice = nullptr;
        _wgpuQueue  = nullptr;
        _backend    = GfxBackend::OpenGL;
        return;
    }

    // ── Configure surface ─────────────────────────────────────────────────────
    auto [w, h] = Window::Get()->GetFramebufferSize();
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

    Console::Get()->Info("[GfxFactory] WebGPU initialized successfully.");
    return;
#endif
    _backend = GfxBackend::OpenGL;
}

#if defined(AE_USE_WEBGPU)
WGPUTextureView GfxFactory::GetCurrentSwapchainView() {
    if (!_surface) return nullptr;
    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(_surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) return nullptr;
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

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
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

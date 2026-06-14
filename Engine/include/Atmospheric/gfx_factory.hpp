#pragma once
#include "buffer.hpp"
#include "render_target.hpp"
#include "window.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

#ifndef __EMSCRIPTEN__
struct SDL_Window;
#endif
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#endif

// Static factory — call Init() once after the window is ready, then use
// CreateBuffer() / CreateRenderTarget() everywhere that needs a GPU resource.
//
// AE_USE_WEBGPU (CMake option, default OFF):
//   Controls whether WebGPU support is compiled in.
//   The actual backend is chosen at runtime:
//     - WebGPU support compiled in AND browser/device reports availability
//       → attempts WebGPU; falls back to OpenGL/WebGL 2 on failure
//     - WebGPU support not compiled in, OR unavailable at runtime
//       → OpenGL 4.1 (native) / WebGL 2 (Emscripten)
class GfxFactory {
public:
#ifdef __EMSCRIPTEN__
    // Web: requests WebGPU adapter+device synchronously (requires -sASYNCIFY=1);
    // falls back to WebGL 2 if WebGPU is unavailable or any step fails.
    static void Init();
#if defined(AE_USE_WEBGPU)
    static WGPUDevice        GetWebGPUDevice()      { return _wgpuDevice; }
    static WGPUQueue         GetWebGPUQueue()        { return _wgpuQueue; }
    static WGPUTextureFormat GetSwapchainFormat()    { return _swapchainFormat; }
    // Returns the current swapchain texture view (caller must wgpuTextureViewRelease).
    // Returns nullptr if the surface isn't ready yet.
    static WGPUTextureView   GetCurrentSwapchainView();
    static void              PresentSwapchain();
#endif
#else
    // Native: stores sdlWindow for future Dawn surface creation.
    // Currently always falls back to OpenGL until Dawn is integrated.
    static void Init(SDL_Window* sdlWindow = nullptr);
#endif

    static void Shutdown();

    static GfxBackend GetBackend() { return _backend; }

    static std::unique_ptr<Buffer>       CreateBuffer();
    static std::unique_ptr<RenderTarget> CreateRenderTarget(const RenderTarget::Props& props);

    // Cross-backend texture upload.
    //   OpenGL  path: creates a GL texture and returns the GL handle.
    //   WebGPU  path: creates a WGPUTexture, stores it internally, returns
    //                 a synthetic ID usable with GetWGPUTexture().
    // pixels must be RGBA8 (4 bytes per pixel, w*h pixels).
    static uint32_t  UploadTexture2D(const uint8_t* pixels, int w, int h);

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // Returns the WGPUTexture previously registered via UploadTexture2D().
    // Returns nullptr if the ID is unknown.
    static WGPUTexture GetWGPUTexture(uint32_t id);
#endif

private:
    static GfxBackend _backend;

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    static WGPUDevice        _wgpuDevice;
    static WGPUQueue         _wgpuQueue;
    static WGPUSurface       _surface;
    static WGPUTextureFormat _swapchainFormat;
    static std::unordered_map<uint32_t, WGPUTexture> _gpuTextures;
    static uint32_t          _nextTexID;
#elif !defined(__EMSCRIPTEN__)
    static SDL_Window* _sdlWindow;
#endif
};

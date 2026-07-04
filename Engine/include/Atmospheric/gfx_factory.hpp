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
    static WGPUDevice GetWebGPUDevice() {
        return _wgpuDevice;
    }
    static WGPUQueue GetWebGPUQueue() {
        return _wgpuQueue;
    }
    static WGPUTextureFormat GetSwapchainFormat() {
        return _swapchainFormat;
    }
    // Returns the current swapchain texture view (caller must wgpuTextureViewRelease).
    // Returns nullptr if the surface isn't ready yet.
    static WGPUTextureView GetCurrentSwapchainView();
    static void PresentSwapchain();
#endif
#else
    // Native: stores sdlWindow for future Dawn surface creation.
    // Currently always falls back to OpenGL until Dawn is integrated.
    static void Init(SDL_Window* sdlWindow = nullptr);
#endif

    static void Shutdown();

    static GfxBackend GetBackend() {
        return _backend;
    }

    static std::unique_ptr<Buffer> CreateBuffer();
    static std::unique_ptr<RenderTarget> CreateRenderTarget(const RenderTarget::Props& props);

    // Cross-backend texture upload.
    //   OpenGL  path: creates a GL texture and returns the GL handle.
    //   WebGPU  path: creates a WGPUTexture, stores it internally, returns
    //                 a synthetic ID usable with GetWGPUTexture().
    // pixels must be RGBA8 (4 bytes per pixel, w*h pixels).
    // `filter` records how the texture should be sampled (see TextureFilter):
    // on GL it's applied via glTexParameteri; on WebGPU it's remembered so
    // GPUCanvasPass can bind a matching sampler. Defaults to Linear.
    static uint32_t UploadTexture2D(const uint8_t* pixels, int w, int h, TextureFilter filter = TextureFilter::Linear);

    // Filter hint recorded for a texture at upload time. Returns Linear for
    // unknown IDs. Consulted by GPUCanvasPass to pick its sampler; on GL the
    // filter is already baked into the texture, so callers rarely need this.
    static TextureFilter GetTextureFilter(uint32_t id);

    // Cross-backend update of an existing texture's pixel contents. id must
    // have come from UploadTexture2D(). pixels must be RGBA8 (4 bytes per
    // pixel, w*h pixels). If w/h differ from the size the texture was
    // created with, storage is reallocated under the same id (the WebGPU
    // texture object is immutable, so this releases and recreates it
    // internally; callers don't need to know or care).
    //   OpenGL  path: glTexImage2D (always reallocates; simplest way to
    //                 handle same-size updates and resizes uniformly).
    //   WebGPU  path: wgpuQueueWriteTexture, recreating the WGPUTexture
    //                 first if the size changed.
    static void UpdateTexture2D(uint32_t id, const uint8_t* pixels, int w, int h);

    // Cross-backend texture release. id must have come from UploadTexture2D()
    // or UploadCompressedTexture2D().
    //   OpenGL path: glDeleteTextures.
    //   WebGPU path: releases the WGPUTexture and erases it from the registry.
    static void ReleaseTexture(uint32_t id);

    // Block-compressed texture format negotiated with the WebGPU adapter/device
    // at Init() time (queried via wgpuAdapterHasFeature, not just requested).
    // TextureCompressionFormat::None on the OpenGL backend, or on WebGPU if the
    // adapter granted none of the BC/ETC2/ASTC features.
    static TextureCompressionFormat GetSupportedCompressionFormat() {
        return _compressionFormat;
    }

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    // Returns the WGPUTexture previously registered via UploadTexture2D()
    // or UploadCompressedTexture2D().
    // Returns nullptr if the ID is unknown.
    static WGPUTexture GetWGPUTexture(uint32_t id);

    // WebGPU-only: uploads pre-transcoded block-compressed texture data.
    // `format` must match GetSupportedCompressionFormat() (caller transcodes
    // accordingly); `w`/`h` must be multiples of 4 (the block size for all of
    // BC7/ETC2/ASTC4x4). Returns a synthetic ID usable with GetWGPUTexture()
    // and ReleaseTexture(), or 0 if format is None or the device lacks the
    // feature.
    static uint32_t
        UploadCompressedTexture2D(TextureCompressionFormat format, const uint8_t* data, size_t dataSize, int w, int h);

    // WebGPU-only: single-channel half-float texture from float data (heights,
    // masks) — the same GL_R16F path the GLES/WebGL2 backend uses for
    // heightmaps. r16float is filterable core WebGPU, so it binds like any
    // other sampled texture. float→f16 conversion happens internally.
    // Returns a synthetic ID usable with GetWGPUTexture()/ReleaseTexture()/
    // UpdateTextureR16F().
    static uint32_t UploadTextureR16F(const float* texels, int w, int h);

    // Update (and resize, same semantics as UpdateTexture2D) an r16float
    // texture created by UploadTextureR16F().
    static void UpdateTextureR16F(uint32_t id, const float* texels, int w, int h);
#endif

private:
    static GfxBackend _backend;
    static TextureCompressionFormat _compressionFormat;

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    static WGPUDevice _wgpuDevice;
    static WGPUQueue _wgpuQueue;
    static WGPUSurface _surface;
    static WGPUTextureFormat _swapchainFormat;
    struct GpuTexEntry {
        WGPUTexture tex = nullptr;
        TextureFilter filter = TextureFilter::Linear;
    };
    static std::unordered_map<uint32_t, GpuTexEntry> _gpuTextures;
    static uint32_t _nextTexID;
#elif !defined(__EMSCRIPTEN__)
    static SDL_Window* _sdlWindow;
#endif
};

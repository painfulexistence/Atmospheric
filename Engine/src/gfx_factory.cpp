#include "gfx_factory.hpp"
#include "console_subsystem.hpp"
#include "gl_buffer.hpp"
#include "gl_render_target.hpp"
#include "globals.hpp"// glad / GLES3
#include <vector>

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_buffer.hpp"
#include "gpu_render_target.hpp"
#include <webgpu/webgpu.h>
#endif

// ── Static member definitions ────────────────────────────────────────────────
GfxBackend GfxFactory::_backend = GfxBackend::OpenGL;
TextureCompressionFormat GfxFactory::_compressionFormat = TextureCompressionFormat::None;

#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
WGPUDevice GfxFactory::_wgpuDevice = nullptr;
WGPUQueue GfxFactory::_wgpuQueue = nullptr;
WGPUSurface GfxFactory::_surface = nullptr;
WGPUTextureFormat GfxFactory::_swapchainFormat = WGPUTextureFormat_BGRA8Unorm;
std::unordered_map<uint32_t, GfxFactory::GpuTexEntry> GfxFactory::_gpuTextures;
uint32_t GfxFactory::_nextTexID = 1;
#elif !defined(__EMSCRIPTEN__)
SDL_Window* GfxFactory::_sdlWindow = nullptr;
#endif

// ── Init ─────────────────────────────────────────────────────────────────────
#ifdef __EMSCRIPTEN__

void GfxFactory::Init() {
#if defined(AE_USE_WEBGPU)
    // ── Create instance ───────────────────────────────────────────────────────
    WGPUInstanceFeatureName requiredFeatures[] = { WGPUInstanceFeatureName_TimedWaitAny };
    WGPUInstanceLimits instLimits{};
    instLimits.timedWaitAnyMaxCount = 1;
    WGPUInstanceDescriptor instDesc{};
    instDesc.requiredFeatureCount = 1;
    instDesc.requiredFeatures = requiredFeatures;
    instDesc.requiredLimits = &instLimits;
    WGPUInstance inst = wgpuCreateInstance(&instDesc);
    if (!inst) {
        ConsoleSubsystem::Get()->Warn("[GfxFactory] wgpuCreateInstance failed. Falling back to WebGL 2.");
        _backend = GfxBackend::OpenGL;
        return;
    }

    // ── Request adapter (Future API — requires -sASYNCIFY=1) ─────────────────
    WGPURequestAdapterOptions adapterOpts{};
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterCallbackInfo adapterCbInfo{};
    adapterCbInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    adapterCbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView, void* ud1, void*) {
        if (status == WGPURequestAdapterStatus_Success) *static_cast<WGPUAdapter*>(ud1) = a;
    };
    adapterCbInfo.userdata1 = &adapter;

    WGPUFutureWaitInfo adapterWait{};
    adapterWait.future = wgpuInstanceRequestAdapter(inst, &adapterOpts, adapterCbInfo);
    wgpuInstanceWaitAny(inst, 1, &adapterWait, UINT64_MAX);

    if (!adapter) {
        ConsoleSubsystem::Get()->Warn("[GfxFactory] No WebGPU adapter. Falling back to WebGL 2.");
        wgpuInstanceRelease(inst);
        _backend = GfxBackend::OpenGL;
        return;
    }

    // ── Negotiate block-compressed texture support ───────────────────────────
    // Query (not just hope for) BC/ETC2/ASTC before requesting the device —
    // wgpuAdapterHasFeature reflects what the adapter can actually grant.
    // Preference order: BC (most common on desktop browsers) > ETC2 > ASTC.
    std::vector<WGPUFeatureName> deviceFeatures;
    TextureCompressionFormat negotiatedCompression = TextureCompressionFormat::None;
    if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TextureCompressionBC)) {
        deviceFeatures.push_back(WGPUFeatureName_TextureCompressionBC);
        negotiatedCompression = TextureCompressionFormat::BC7;
    } else if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TextureCompressionETC2)) {
        deviceFeatures.push_back(WGPUFeatureName_TextureCompressionETC2);
        negotiatedCompression = TextureCompressionFormat::ETC2;
    } else if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TextureCompressionASTC)) {
        deviceFeatures.push_back(WGPUFeatureName_TextureCompressionASTC);
        negotiatedCompression = TextureCompressionFormat::ASTC4x4;
    }

    // ── Request device ────────────────────────────────────────────────────────
    WGPUDevice device = nullptr;
    WGPURequestDeviceCallbackInfo deviceCbInfo{};
    deviceCbInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView, void* ud1, void*) {
        if (status == WGPURequestDeviceStatus_Success) *static_cast<WGPUDevice*>(ud1) = d;
    };
    deviceCbInfo.userdata1 = &device;

    WGPUDeviceDescriptor deviceDesc{};
    deviceDesc.requiredFeatureCount = deviceFeatures.size();
    deviceDesc.requiredFeatures = deviceFeatures.empty() ? nullptr : deviceFeatures.data();
    WGPUFutureWaitInfo deviceWait{};
    deviceWait.future = wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCbInfo);
    wgpuInstanceWaitAny(inst, 1, &deviceWait, UINT64_MAX);
    wgpuAdapterRelease(adapter);

    if (!device) {
        ConsoleSubsystem::Get()->Warn("[GfxFactory] WebGPU device creation failed. Falling back to WebGL 2.");
        wgpuInstanceRelease(inst);
        _backend = GfxBackend::OpenGL;
        return;
    }

    ConsoleSubsystem::Get()->Info("[GfxFactory] WebGPU adapter and device acquired.");
    _backend = GfxBackend::WebGPU;
    _wgpuDevice = device;
    _wgpuQueue = wgpuDeviceGetQueue(device);
    // Device creation can silently grant fewer features than requested — only
    // trust what wgpuDeviceHasFeature reports back, not what we asked for.
    if (negotiatedCompression != TextureCompressionFormat::None && !wgpuDeviceHasFeature(device, deviceFeatures[0])) {
        ConsoleSubsystem::Get()->Warn("[GfxFactory] Device did not grant requested texture compression feature.");
        negotiatedCompression = TextureCompressionFormat::None;
    }
    _compressionFormat = negotiatedCompression;
    const char* compressionName = negotiatedCompression == TextureCompressionFormat::BC7    ? "BC7"
                                  : negotiatedCompression == TextureCompressionFormat::ETC2 ? "ETC2"
                                  : negotiatedCompression == TextureCompressionFormat::ASTC4x4
                                      ? "ASTC4x4"
                                      : "none (RGBA32 fallback)";
    ConsoleSubsystem::Get()->Info(std::string("[GfxFactory] Texture compression: ") + compressionName);

    // ── Create surface ────────────────────────────────────────────────────────
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = { "#canvas", WGPU_STRLEN };
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
    _surface = wgpuInstanceCreateSurface(inst, &surfDesc);
    // Deliberately keep the instance alive (leaked for the app lifetime):
    // emdawnwebgpu requires a live WGPUInstance for surface/present handling —
    // releasing the last external reference here triggers "A valid external
    // Instance reference no longer exists" and the swapchain never composites.

    if (!_surface) {
        ConsoleSubsystem::Get()->Warn("[GfxFactory] wgpuInstanceCreateSurface failed. Falling back to WebGL 2.");
        wgpuDeviceRelease(device);
        _wgpuDevice = nullptr;
        _wgpuQueue = nullptr;
        _backend = GfxBackend::OpenGL;
        return;
    }

    // ── Configure surface ─────────────────────────────────────────────────────
    auto [w, h] = Window::Get()->GetPhysicalSize();
    _swapchainFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfiguration cfg{};
    cfg.device = device;
    cfg.format = _swapchainFormat;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.width = static_cast<uint32_t>(w);
    cfg.height = static_cast<uint32_t>(h);
    cfg.presentMode = WGPUPresentMode_Fifo;
    cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(_surface, &cfg);

    ConsoleSubsystem::Get()->Info("[GfxFactory] WebGPU initialized successfully.");
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
#ifndef __EMSCRIPTEN__
    if (_surface) wgpuSurfacePresent(_surface);
#endif
    // On Emscripten the browser's requestAnimationFrame loop handles presentation
}
#endif// AE_USE_WEBGPU

#else// native

void GfxFactory::Init(SDL_Window* sdlWindow) {
    _sdlWindow = sdlWindow;

    // TODO: Initialize Dawn WebGPU here once Dawn is integrated.
    _backend = GfxBackend::OpenGL;
}

#endif// __EMSCRIPTEN__

// ── Shutdown ─────────────────────────────────────────────────────────────────
void GfxFactory::Shutdown() {
#if defined(__EMSCRIPTEN__) && defined(AE_USE_WEBGPU)
    for (auto& [id, entry] : _gpuTextures)
        wgpuTextureRelease(entry.tex);
    _gpuTextures.clear();
    if (_surface) {
        wgpuSurfaceRelease(_surface);
        _surface = nullptr;
    }
    if (_wgpuDevice) {
        wgpuDeviceRelease(_wgpuDevice);
        _wgpuDevice = nullptr;
    }
    _wgpuQueue = nullptr;
#elif !defined(__EMSCRIPTEN__)
    _sdlWindow = nullptr;
#endif
}

// ── Cross-backend texture upload ─────────────────────────────────────────────
TextureHandle GfxFactory::UploadTexture2D(const uint8_t* pixels, int w, int h, TextureFilter filter) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) {
        uint32_t id = _nextTexID++;

        WGPUTextureDescriptor td{};
        td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(_wgpuDevice, &td);

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<uint32_t>(w) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuQueueWriteTexture(_wgpuQueue, &dst, pixels, static_cast<size_t>(w) * h * 4, &layout, &extent);

        _gpuTextures[id] = { tex, filter };
        return TextureHandle{ id };
    }
#endif
    // OpenGL / WebGL path — filter is baked into the texture object.
    const GLint glFilter = (filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return TextureHandle{ static_cast<uint32_t>(texID) };
}

uint32_t GfxFactory::UploadTextureRGBA32F(const float* rgba, int w, int h) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) {
        uint32_t id = _nextTexID++;

        WGPUTextureDescriptor td{};
        td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        td.format = WGPUTextureFormat_RGBA32Float;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(_wgpuDevice, &td);

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<uint32_t>(w) * 4 * 4;// 4 channels * 4 bytes
        layout.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuQueueWriteTexture(_wgpuQueue, &dst, rgba, static_cast<size_t>(w) * h * 4 * sizeof(float), &layout, &extent);

        // rgba32float is non-filterable; record Nearest so any sampler-based
        // consumer picks a matching (non-filtering) sampler. VAT samples it with
        // textureLoad, which needs no sampler at all.
        _gpuTextures[id] = { tex, TextureFilter::Nearest };
        return id;
    }
#endif
    // OpenGL / WebGL path — NEAREST + CLAMP baked into the texture object.
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uint32_t>(texID);
}

TextureFilter GfxFactory::GetTextureFilter(TextureHandle id) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    auto it = _gpuTextures.find(id.id);
    if (it != _gpuTextures.end()) return it->second.filter;
#else
    (void)id;
#endif
    return TextureFilter::Linear;
}

void GfxFactory::UpdateTexture2D(TextureHandle id, const uint8_t* pixels, int w, int h) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) {
        auto it = _gpuTextures.find(id.id);
        if (it == _gpuTextures.end()) return;

        WGPUTexture tex = it->second.tex;
        if (wgpuTextureGetWidth(tex) != static_cast<uint32_t>(w)
            || wgpuTextureGetHeight(tex) != static_cast<uint32_t>(h)) {
            // WGPUTexture storage is immutable — recreate under the same
            // synthetic id so callers don't need to track resizes. Create the
            // replacement BEFORE releasing the old texture: handles are object-
            // table slots, so release-then-create can hand the new texture the
            // old handle value, and pointer-compare bind-group caches (canvas /
            // forward passes) would keep sampling the stale texture forever.
            WGPUTextureDescriptor td{};
            td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            td.dimension = WGPUTextureDimension_2D;
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            WGPUTexture newTex = wgpuDeviceCreateTexture(_wgpuDevice, &td);
            wgpuTextureRelease(tex);
            tex = newTex;
            it->second.tex = tex;// filter hint preserved
        }

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<uint32_t>(w) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuQueueWriteTexture(_wgpuQueue, &dst, pixels, static_cast<size_t>(w) * h * 4, &layout, &extent);
        return;
    }
#endif
    // OpenGL / WebGL path
    auto texID = static_cast<GLuint>(id.id);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
WGPUTexture GfxFactory::GetWGPUTexture(TextureHandle id) {
    auto it = _gpuTextures.find(id.id);
    return (it != _gpuTextures.end()) ? it->second.tex : nullptr;
}

// IEEE 754 float → half. Heights/masks are finite and mostly 0-1, so denorms
// flush to zero and out-of-range values saturate to ±inf.
static uint16_t FloatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    const uint16_t sign = static_cast<uint16_t>((x >> 16) & 0x8000);
    const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
    const uint16_t man = static_cast<uint16_t>((x >> 13) & 0x3FF);
    if (exp <= 0) return sign;// underflow → signed zero
    if (exp >= 31) return sign | 0x7C00;// overflow → inf
    return sign | static_cast<uint16_t>(exp << 10) | man;
}

static WGPUTexture CreateR16FTexture(WGPUDevice device, WGPUQueue queue, const float* texels, int w, int h) {
    WGPUTextureDescriptor td{};
    td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    td.format = WGPUTextureFormat_R16Float;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);

    std::vector<uint16_t> halves(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < halves.size(); ++i)
        halves[i] = FloatToHalf(texels[i]);

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = tex;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<uint32_t>(w) * 2;
    layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    wgpuQueueWriteTexture(queue, &dst, halves.data(), halves.size() * 2, &layout, &extent);
    return tex;
}

TextureHandle GfxFactory::UploadTextureR16F(const float* texels, int w, int h) {
    if (!_wgpuDevice) return {};
    uint32_t id = _nextTexID++;
    _gpuTextures[id] = { CreateR16FTexture(_wgpuDevice, _wgpuQueue, texels, w, h), TextureFilter::Linear };
    return TextureHandle{ id };
}

void GfxFactory::UpdateTextureR16F(TextureHandle id, const float* texels, int w, int h) {
    if (!_wgpuDevice) return;
    auto it = _gpuTextures.find(id.id);
    if (it == _gpuTextures.end()) return;

    WGPUTexture tex = it->second.tex;
    if (wgpuTextureGetWidth(tex) != static_cast<uint32_t>(w) || wgpuTextureGetHeight(tex) != static_cast<uint32_t>(h)) {
        // Same create-before-release ordering as UpdateTexture2D: handles are
        // object-table slots, and pointer-compare bind-group caches must see
        // a new value on resize.
        WGPUTexture newTex = CreateR16FTexture(_wgpuDevice, _wgpuQueue, texels, w, h);
        wgpuTextureRelease(tex);
        it->second.tex = newTex;
        return;
    }

    std::vector<uint16_t> halves(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < halves.size(); ++i)
        halves[i] = FloatToHalf(texels[i]);
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = tex;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<uint32_t>(w) * 2;
    layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    wgpuQueueWriteTexture(_wgpuQueue, &dst, halves.data(), halves.size() * 2, &layout, &extent);
}

TextureHandle GfxFactory::UploadCompressedTexture2D(
    TextureCompressionFormat format, const uint8_t* data, size_t dataSize, int w, int h
) {
    if (format == TextureCompressionFormat::None || !_wgpuDevice) return {};

    WGPUTextureFormat wgpuFormat;
    switch (format) {
    case TextureCompressionFormat::BC7:
        wgpuFormat = WGPUTextureFormat_BC7RGBAUnorm;
        break;
    case TextureCompressionFormat::ETC2:
        wgpuFormat = WGPUTextureFormat_ETC2RGBA8Unorm;
        break;
    case TextureCompressionFormat::ASTC4x4:
        wgpuFormat = WGPUTextureFormat_ASTC4x4Unorm;
        break;
    default:
        return {};
    }

    uint32_t id = _nextTexID++;

    WGPUTextureDescriptor td{};
    td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    td.format = wgpuFormat;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(_wgpuDevice, &td);

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = tex;
    dst.aspect = WGPUTextureAspect_All;

    // BC7 / ETC2RGBA8 / ASTC4x4 all use 4x4 blocks at 16 bytes/block.
    const uint32_t blocksWide = (static_cast<uint32_t>(w) + 3) / 4;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = blocksWide * 16;
    layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    wgpuQueueWriteTexture(_wgpuQueue, &dst, data, dataSize, &layout, &extent);

    // Compressed textures are photographic/material maps → Linear.
    _gpuTextures[id] = { tex, TextureFilter::Linear };
    return TextureHandle{ id };
}
#endif

void GfxFactory::ReleaseTexture(TextureHandle id) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU) {
        auto it = _gpuTextures.find(id.id);
        if (it != _gpuTextures.end()) {
            wgpuTextureRelease(it->second.tex);
            _gpuTextures.erase(it);
        }
        return;
    }
#endif
    auto texID = static_cast<GLuint>(id.id);
    glDeleteTextures(1, &texID);
}

// ── Factory methods ──────────────────────────────────────────────────────────
std::unique_ptr<Buffer> GfxFactory::CreateBuffer() {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) return std::make_unique<GPUBuffer>(_wgpuDevice, _wgpuQueue);
#elif !defined(__EMSCRIPTEN__)
    (void)_sdlWindow;
#endif
    return std::make_unique<GLBuffer>();
}

std::unique_ptr<RenderTarget> GfxFactory::CreateRenderTarget(const RenderTarget::Props& props) {
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (_backend == GfxBackend::WebGPU && _wgpuDevice) return std::make_unique<GPURenderTarget>(_wgpuDevice, props);
#elif !defined(__EMSCRIPTEN__)
    (void)_sdlWindow;
#endif
    return std::make_unique<GLRenderTarget>(props);
}

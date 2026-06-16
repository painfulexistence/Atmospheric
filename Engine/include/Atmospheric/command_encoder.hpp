#pragma once

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#endif

// Per-frame GPU recording context threaded through Draw() and Begin() calls.
//
// OpenGL is an implicit-state API — no explicit command recording.
// GLCommandEncoder is therefore empty; pass nullptr or a default instance.
//
// WebGPU uses explicit command encoders and render pass encoders.
// Populate before calling Begin() / Draw():
//   enc.encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
//   // Begin() sets enc.pass via wgpuCommandEncoderBeginRenderPass.
class CommandEncoder {
public:
    virtual ~CommandEncoder() = default;
};

// OpenGL: no recording state needed.
class GLCommandEncoder : public CommandEncoder {};

// WebGPU per-frame recording handles (browser Emscripten path for now;
// same struct will be used for native Dawn once integrated).
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
struct GPUCommandEncoder : public CommandEncoder {
    WGPUCommandEncoder    encoder = nullptr;
    WGPURenderPassEncoder pass    = nullptr;
};
#endif
// TODO: add native Dawn GPUCommandEncoder guard once Dawn is integrated.

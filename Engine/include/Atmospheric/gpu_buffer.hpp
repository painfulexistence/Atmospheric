#pragma once
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "buffer.hpp"
#include "command_encoder.hpp"
#include <webgpu/webgpu.h>

// WebGPU implementation of Buffer (Emscripten + AE_USE_WEBGPU).
//
// Upload path:  wgpuQueueWriteBuffer — synchronous write from JS side.
// Draw path:    binds vertex / index buffers to the active WGPURenderPassEncoder
//               via the GPUCommandEncoder supplied to Draw().
//               Topology is encoded in the WGPURenderPipeline descriptor; the
//               topology parameter here is ignored at draw time.
class GPUBuffer : public Buffer {
public:
    GPUBuffer(WGPUDevice device, WGPUQueue queue);
    ~GPUBuffer() override;

    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;

    void Initialize(VertexFormat format, BufferUsage usage = BufferUsage::Static) override;
    void Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) override;
    void Upload(
        const void* vertexData, size_t vertexCount, size_t vertexSize, const uint16_t* indexData, size_t indexCount
    ) override;
    void Draw(CommandEncoder* enc = nullptr, PrimitiveTopology topology = PrimitiveTopology::Triangles) const override;

    bool IsInitialized() const override {
        return _initialized;
    }
    size_t GetVertexCount() const override {
        return _vertexCount;
    }
    size_t GetIndexCount() const override {
        return _indexCount;
    }
    VertexFormat GetFormat() const override {
        return _format;
    }

private:
    WGPUBuffer AllocAndUpload(const void* data, size_t bytes, WGPUBufferUsage usage);

    WGPUDevice _device = nullptr;
    WGPUQueue _queue = nullptr;
    WGPUBuffer _vertexBuffer = nullptr;
    WGPUBuffer _indexBuffer = nullptr;

    VertexFormat _format = VertexFormat::Standard;
    size_t _vertexCount = 0;
    size_t _indexCount = 0;
    bool _initialized = false;
    bool _hasIndices = false;
};
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

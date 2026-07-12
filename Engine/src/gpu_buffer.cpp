#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "gpu_buffer.hpp"

GPUBuffer::GPUBuffer(WGPUDevice device, WGPUQueue queue) : _device(device), _queue(queue) {
}

GPUBuffer::~GPUBuffer() {
    if (_vertexBuffer) {
        wgpuBufferRelease(_vertexBuffer);
        _vertexBuffer = nullptr;
    }
    if (_indexBuffer) {
        wgpuBufferRelease(_indexBuffer);
        _indexBuffer = nullptr;
    }
    if (_instanceBuffer) {
        wgpuBufferRelease(_instanceBuffer);
        _instanceBuffer = nullptr;
    }
}

void GPUBuffer::Initialize(VertexFormat format, BufferUsage /*usage*/) {
    _format = format;
    _initialized = true;
}

WGPUBuffer GPUBuffer::AllocAndUpload(const void* data, size_t bytes, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc{};
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = bytes;
    desc.mappedAtCreation = false;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(_device, &desc);
    wgpuQueueWriteBuffer(_queue, buf, 0, data, bytes);
    return buf;
}

void GPUBuffer::Upload(const void* vertexData, size_t vertexCount, size_t vertexSize) {
    if (!_initialized) Initialize(_format);
    if (_vertexBuffer) {
        wgpuBufferRelease(_vertexBuffer);
        _vertexBuffer = nullptr;
    }
    _vertexCount = vertexCount;
    _hasIndices = false;
    _vertexBuffer = AllocAndUpload(vertexData, vertexCount * vertexSize, WGPUBufferUsage_Vertex);
}

void GPUBuffer::Upload(
    const void* vertexData, size_t vertexCount, size_t vertexSize, const uint16_t* indexData, size_t indexCount
) {
    if (!_initialized) Initialize(_format);
    if (_vertexBuffer) {
        wgpuBufferRelease(_vertexBuffer);
        _vertexBuffer = nullptr;
    }
    if (_indexBuffer) {
        wgpuBufferRelease(_indexBuffer);
        _indexBuffer = nullptr;
    }
    _vertexCount = vertexCount;
    _indexCount = indexCount;
    _hasIndices = true;
    _vertexBuffer = AllocAndUpload(vertexData, vertexCount * vertexSize, WGPUBufferUsage_Vertex);
    _indexBuffer = AllocAndUpload(indexData, indexCount * sizeof(uint16_t), WGPUBufferUsage_Index);
}

void GPUBuffer::UploadInstances(const void* instanceData, size_t instanceCount, size_t instanceSize) {
    if (!_initialized) Initialize(_format);
    const size_t bytes = instanceCount * instanceSize;
    if (bytes == 0) {
        _instanceCount = 0;
        return;// keep the buffer for the next refill (pooled cells shrink to 0 transiently)
    }
    if (_instanceBuffer && _instanceBufferBytes == bytes) {
        wgpuQueueWriteBuffer(_queue, _instanceBuffer, 0, instanceData, bytes);
    } else {
        if (_instanceBuffer) wgpuBufferRelease(_instanceBuffer);
        _instanceBuffer = AllocAndUpload(instanceData, bytes, WGPUBufferUsage_Vertex);
        _instanceBufferBytes = bytes;
    }
    _instanceCount = instanceCount;
}

// topology is ignored: in WebGPU, primitive topology is baked into the render
// pipeline (see GpuPipelineBuilder), not chosen per draw like glDrawArrays.
void GPUBuffer::Draw(CommandEncoder* enc, PrimitiveTopology /*topology*/) const {
    auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
    if (!gpuEnc || !gpuEnc->pass || !_vertexBuffer) return;
    // An instanced buffer with zero live instances draws nothing — mirrors
    // GLBuffer, and avoids a draw the instance-stepped pipeline can't
    // validate (slot 1 would be unbound or stale-sized).
    if (_instanceBuffer && _instanceCount == 0) return;
    WGPURenderPassEncoder pass = gpuEnc->pass;
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, _vertexBuffer, 0, WGPU_WHOLE_SIZE);
    // Slot 1 carries per-instance data; only valid against a pipeline whose
    // vertex state declared the instance-stepped buffer (GpuPipelineBuilder::
    // instance()). Non-instanced pipelines never see a buffer here because
    // their meshes never call UploadInstances.
    const bool instanced = _instanceBuffer && _instanceCount > 0;
    if (instanced) wgpuRenderPassEncoderSetVertexBuffer(pass, 1, _instanceBuffer, 0, WGPU_WHOLE_SIZE);
    const uint32_t instances = instanced ? static_cast<uint32_t>(_instanceCount) : 1u;
    if (_hasIndices) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, _indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, static_cast<uint32_t>(_indexCount), instances, 0, 0, 0);
    } else {
        wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(_vertexCount), instances, 0, 0);
    }
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

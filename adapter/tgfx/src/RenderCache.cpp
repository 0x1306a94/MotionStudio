#include "RenderCache.h"

#include <cstring>

#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>

namespace motion {

namespace {

constexpr float FULLSCREEN_TRIANGLE_VERTICES[] = {
    -1.0f, -1.0f,  // clip (-1,-1) → uv (0,0)
    3.0f, -1.0f,   // clip (3,-1)  → uv (2,0)
    -1.0f, 3.0f,   // clip (-1,3)  → uv (0,2)
};

// Metal constant-buffer offsets are typically 256-byte aligned.
constexpr size_t UNIFORM_OFFSET_ALIGNMENT = 256;
constexpr size_t UNIFORM_PACKET_BUFFER_SIZE = 64 * 1024;

size_t AlignUp(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

RenderCache::RenderCache() {
}

RenderCache::~RenderCache() {
    releaseAll();
}

void RenderCache::attachToContext(tgfx::Context *current, bool /*forDrawing*/) {
    if (current == nullptr) {
        return;
    }
    if (contextID_ > 0 && contextID_ != current->uniqueID()) {
        // Context/device changed: drop GPU objects. Compare uniqueID rather than
        // pointer because a destroyed+recreated context may reuse the same address.
        releaseAll();
    }

    context_ = current;
    contextID_ = current->uniqueID();
}

void RenderCache::detachFromContext() {
    context_ = nullptr;
}

std::shared_ptr<tgfx::RenderPipeline> RenderCache::findColorSourcePipeline(EntityId shaderId) const {
    if (!shaderId.isValid()) {
        return nullptr;
    }
    auto result = colorSourcePipelineMap_.find(shaderId);
    if (result != colorSourcePipelineMap_.end()) {
        return result->second;
    }
    return nullptr;
}

void RenderCache::addColorSourcePipeline(EntityId shaderId, std::shared_ptr<tgfx::RenderPipeline> pipeline) {
    if (!shaderId.isValid() || pipeline == nullptr) {
        return;
    }
    colorSourcePipelineMap_[shaderId] = std::move(pipeline);
}

void RenderCache::invalidateColorSourcePipeline(EntityId shaderId) {
    if (!shaderId.isValid()) {
        return;
    }
    colorSourcePipelineMap_.erase(shaderId);
}

UniformBufferSlice RenderCache::acquireUniformSlice(tgfx::GPU *gpu, size_t size) {
    UniformBufferSlice slice;
    if (gpu == nullptr || size == 0) {
        return slice;
    }

    const size_t alignedSize = AlignUp(size, UNIFORM_OFFSET_ALIGNMENT);

    // Oversized uploads get a dedicated buffer (not pooled).
    if (alignedSize > UNIFORM_PACKET_BUFFER_SIZE) {
        slice.buffer = gpu->createBuffer(alignedSize, tgfx::GPUBufferUsage::UNIFORM);
        slice.offset = 0;
        return slice;
    }

    auto &packet = uniformPackets_[uniformPacketIndex_];
    if (packet.buffers.empty()) {
        auto buffer = gpu->createBuffer(UNIFORM_PACKET_BUFFER_SIZE, tgfx::GPUBufferUsage::UNIFORM);
        if (buffer == nullptr) {
            return slice;
        }
        packet.buffers.push_back(std::move(buffer));
        packet.bufferIndex = 0;
        packet.cursor = 0;
    }

    if (packet.bufferIndex < packet.buffers.size() &&
        packet.cursor + alignedSize <= packet.buffers[packet.bufferIndex]->size()) {
        slice.buffer = packet.buffers[packet.bufferIndex];
        slice.offset = packet.cursor;
        packet.cursor += alignedSize;
        return slice;
    }

    packet.bufferIndex += 1;
    packet.cursor = 0;

    if (packet.bufferIndex >= packet.buffers.size()) {
        auto buffer = gpu->createBuffer(UNIFORM_PACKET_BUFFER_SIZE, tgfx::GPUBufferUsage::UNIFORM);
        if (buffer == nullptr) {
            return slice;
        }
        packet.buffers.push_back(std::move(buffer));
    }

    slice.buffer = packet.buffers[packet.bufferIndex];
    slice.offset = packet.cursor;
    packet.cursor += alignedSize;
    return slice;
}

void RenderCache::advanceUniformFrame() {
    uniformPacketIndex_ = (uniformPacketIndex_ + 1) % static_cast<uint32_t>(uniformPackets_.size());
    auto &packet = uniformPackets_[uniformPacketIndex_];
    packet.bufferIndex = 0;
    packet.cursor = 0;
}

std::shared_ptr<tgfx::GPUBuffer> RenderCache::getFullscreenVertexBuffer(tgfx::GPU *gpu) {
    if (fullscreenVertexBuffer_ != nullptr) {
        return fullscreenVertexBuffer_;
    }
    if (gpu == nullptr) {
        return nullptr;
    }

    auto buffer = gpu->createBuffer(sizeof(FULLSCREEN_TRIANGLE_VERTICES), tgfx::GPUBufferUsage::VERTEX);
    if (buffer == nullptr) {
        return nullptr;
    }
    void *mapped = buffer->map();
    if (mapped == nullptr) {
        return nullptr;
    }
    std::memcpy(mapped, FULLSCREEN_TRIANGLE_VERTICES, sizeof(FULLSCREEN_TRIANGLE_VERTICES));
    buffer->unmap();
    fullscreenVertexBuffer_ = std::move(buffer);
    return fullscreenVertexBuffer_;
}

void RenderCache::releaseAll() {
    colorSourcePipelineMap_.clear();
    for (auto &packet : uniformPackets_) {
        packet.buffers.clear();
        packet.bufferIndex = 0;
        packet.cursor = 0;
    }
    uniformPacketIndex_ = 0;
    fullscreenVertexBuffer_ = nullptr;
    contextID_ = 0;
}

};  // namespace motion

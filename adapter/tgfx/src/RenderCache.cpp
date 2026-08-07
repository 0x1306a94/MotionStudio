#include "RenderCache.h"

#include <cstring>

#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>

#include "gpu/GlobalCache.h"

namespace motion {

namespace {

constexpr float FULLSCREEN_TRIANGLE_VERTICES[] = {
    -1.0f, -1.0f,  // clip (-1,-1) → uv (0,0)
    3.0f, -1.0f,   // clip (3,-1)  → uv (2,0)
    -1.0f, 3.0f,   // clip (-1,3)  → uv (0,2)
};

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

void RenderCache::invalidateColorSourcePipelineIfSourceChanged(EntityId shaderId, const std::string &sourceKey) {
    if (!shaderId.isValid()) {
        return;
    }
    auto it = colorSourceSourceKeys_.find(shaderId);
    if (it != colorSourceSourceKeys_.end() && it->second == sourceKey) {
        return;
    }
    invalidateColorSourcePipeline(shaderId);
    colorSourceSourceKeys_[shaderId] = sourceKey;
}

UniformBufferSlice RenderCache::acquireUniformSlice(size_t size) {
    UniformBufferSlice slice;
    if (context_ == nullptr || context_->globalCache() == nullptr || size == 0) {
        return slice;
    }
    slice.buffer = context_->globalCache()->findOrCreateUniformBuffer(size, &slice.offset);
    return slice;
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
    colorSourceSourceKeys_.clear();
    fullscreenVertexBuffer_ = nullptr;
    contextID_ = 0;
}

};  // namespace motion

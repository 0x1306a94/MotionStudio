#include "RenderCache.h"

#include <cstring>

#include <tgfx/core/Surface.h>
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

constexpr size_t MaxSurfacesPerKey = 4;

uint64_t MakeSurfacePoolKey(int width, int height, tgfx::ColorType colorType) {
    const uint64_t packedWidth = static_cast<uint64_t>(width) & 0xFFFFFull;
    const uint64_t packedHeight = static_cast<uint64_t>(height) & 0xFFFFFull;
    const uint64_t packedColor = static_cast<uint64_t>(colorType) & 0xFFull;
    return (packedColor << 40) | (packedWidth << 20) | packedHeight;
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

FilterResources *RenderCache::findFilterResources(uint32_t type) const {
    auto result = filterResourcesMap_.find(type);
    if (result != filterResourcesMap_.end()) {
        return result->second.get();
    }
    return nullptr;
}

void RenderCache::addFilterResources(uint32_t type, std::unique_ptr<FilterResources> resources) {
    if (resources == nullptr) {
        return;
    }
    filterResourcesMap_[type] = std::move(resources);
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

std::shared_ptr<tgfx::Surface> RenderCache::acquireSurface(int width, int height,
                                                           tgfx::ColorType colorType) {
    if (context_ == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }
    const uint64_t key = MakeSurfacePoolKey(width, height, colorType);
    std::vector<SurfacePoolSlot> &group = surfacePool_[key];
    for (SurfacePoolSlot &slot : group) {
        if (!slot.borrowedThisFrame && slot.surface != nullptr) {
            slot.borrowedThisFrame = true;
            return slot.surface;
        }
    }
    std::shared_ptr<tgfx::Surface> surface = tgfx::Surface::Make(context_, width, height, colorType);
    if (surface == nullptr) {
        return nullptr;
    }
    if (group.size() < MaxSurfacesPerKey) {
        SurfacePoolSlot slot;
        slot.surface = surface;
        slot.borrowedThisFrame = true;
        group.push_back(slot);
    }
    return surface;
}

void RenderCache::beginFrame() {
    for (auto &entry : surfacePool_) {
        for (SurfacePoolSlot &slot : entry.second) {
            slot.borrowedThisFrame = false;
        }
    }
}

void RenderCache::releaseAll() {
    colorSourcePipelineMap_.clear();
    colorSourceSourceKeys_.clear();
    filterResourcesMap_.clear();
    fullscreenVertexBuffer_ = nullptr;
    surfacePool_.clear();
    contextID_ = 0;
}

};  // namespace motion

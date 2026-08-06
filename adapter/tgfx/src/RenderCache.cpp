#include "RenderCache.h"

#include <cstring>

#include <tgfx/gpu/Context.h>
#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>

namespace motion {

namespace {

constexpr float kFullscreenTriangleVertices[] = {
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

ColorSourceEffectResource *RenderCache::findColorSourceEffectResource(EntityId ID) {
    auto result = colorSourceEffectResourceMap_.find(ID);
    if (result != colorSourceEffectResourceMap_.end()) {
        return result->second.get();
    }
    return nullptr;
}

void RenderCache::addColorSourceEffectResource(EntityId ID, std::unique_ptr<ColorSourceEffectResource> resources) {
    if (resources == nullptr) {
        return;
    }
    colorSourceEffectResourceMap_[ID] = std::move(resources);
}

void RenderCache::setMainImageSource(EntityId ID, std::string source) {
    mainImageSourceMap_[ID] = std::move(source);
    // mainImage feeds fragment compilation; drop cached pipeline for this id.
    colorSourceEffectResourceMap_.erase(ID);
}

const std::string *RenderCache::findMainImageSource(EntityId ID) const {
    auto result = mainImageSourceMap_.find(ID);
    if (result != mainImageSourceMap_.end()) {
        return &result->second;
    }
    return nullptr;
}

std::shared_ptr<tgfx::GPUBuffer> RenderCache::getFullscreenVertexBuffer(tgfx::GPU *gpu) {
    if (fullscreenVertexBuffer_ != nullptr) {
        return fullscreenVertexBuffer_;
    }
    if (gpu == nullptr) {
        return nullptr;
    }

    auto buffer = gpu->createBuffer(sizeof(kFullscreenTriangleVertices), tgfx::GPUBufferUsage::VERTEX);
    if (buffer == nullptr) {
        return nullptr;
    }
    void *mapped = buffer->map();
    if (mapped == nullptr) {
        return nullptr;
    }
    std::memcpy(mapped, kFullscreenTriangleVertices, sizeof(kFullscreenTriangleVertices));
    buffer->unmap();
    fullscreenVertexBuffer_ = std::move(buffer);
    return fullscreenVertexBuffer_;
}

void RenderCache::releaseAll() {
    colorSourceEffectResourceMap_.clear();
    fullscreenVertexBuffer_ = nullptr;
    contextID_ = 0;
}

};  // namespace motion

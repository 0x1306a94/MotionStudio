#pragma once

#include "MotionStudio/common/EntityId.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace tgfx {
class RenderPipeline;
class GPUBuffer;
class Context;
class GPU;
};  // namespace tgfx

namespace motion {

/**
 * One bump allocation from Context::globalCache()'s uniform pool.
 * Bind with setUniformBuffer(binding, buffer, offset, size).
 */
struct UniformBufferSlice {
    std::shared_ptr<tgfx::GPUBuffer> buffer = nullptr;
    size_t offset = 0;
};

/**
 * Per-context GPU cache for ColorSourceEffect: shared pipelines keyed by
 * document shader EntityId, and a shared fullscreen triangle VBO.
 * Uniform uploads go through tgfx GlobalCache via acquireUniformSlice().
 */
class RenderCache {

  public:
    explicit RenderCache();
    ~RenderCache();

    void attachToContext(tgfx::Context *current, bool forDrawing = true);

    void detachFromContext();

    tgfx::Context *getContext() const {
        return context_;
    }

    std::shared_ptr<tgfx::RenderPipeline> findColorSourcePipeline(EntityId shaderId) const;

    void addColorSourcePipeline(EntityId shaderId, std::shared_ptr<tgfx::RenderPipeline> pipeline);

    // Drops the cached pipeline for shaderId. Call after the shader source or
    // uniform layout changes so the next draw recompiles.
    void invalidateColorSourcePipeline(EntityId shaderId);

    // Invalidates the pipeline when mainImage/decls fingerprint changes.
    // sourceKey: opaque fingerprint for the current shader source + layout.
    void invalidateColorSourcePipelineIfSourceChanged(EntityId shaderId, const std::string &sourceKey);

    // Forwards to Context::globalCache()->findOrCreateUniformBuffer.
    // Requires attachToContext. Frame reuse is handled by tgfx DrawingBuffer::encode
    // calling resetUniformBuffer().
    UniformBufferSlice acquireUniformSlice(size_t size);

    // Shared clip-space fullscreen triangle VBO for the current context. Created lazily.
    std::shared_ptr<tgfx::GPUBuffer> getFullscreenVertexBuffer(tgfx::GPU *gpu);

    void releaseAll();

  private:
    uint32_t contextID_ = 0;
    tgfx::Context *context_ = nullptr;
    std::unordered_map<EntityId, std::shared_ptr<tgfx::RenderPipeline>> colorSourcePipelineMap_ = {};
    std::unordered_map<EntityId, std::string> colorSourceSourceKeys_ = {};
    std::shared_ptr<tgfx::GPUBuffer> fullscreenVertexBuffer_ = nullptr;
};
};  // namespace motion

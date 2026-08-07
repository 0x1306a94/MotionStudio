#pragma once

#include "effects/ColorSourceEffectResources.h"

#include "MotionStudio/common/EntityId.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tgfx {
class RenderPipeline;
class GPUBuffer;
class Context;
class GPU;
};  // namespace tgfx

namespace motion {

/**
 * Per-context GPU cache for ColorSourceEffect: shared pipelines keyed by
 * document shader EntityId, and a triple-buffered bump allocator for uniform
 * uploads so Metal Shared storage is not rewritten while a prior frame still
 * reads it.
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

    // Bump-allocates aligned space in the current uniform packet. Call
    // advanceUniformFrame() once per submitted frame before encoding the next.
    UniformBufferSlice acquireUniformSlice(tgfx::GPU *gpu, size_t size);

    // Rotates to the next uniform packet and resets its bump cursor.
    void advanceUniformFrame();

    // Shared clip-space fullscreen triangle VBO for the current context. Created lazily.
    std::shared_ptr<tgfx::GPUBuffer> getFullscreenVertexBuffer(tgfx::GPU *gpu);

    void releaseAll();

  private:
    // Mirrors tgfx GlobalCache::UniformBufferPacket (see
    // third_party/libpag/third_party/tgfx/src/gpu/GlobalCache.h).
    struct UniformBufferPacket {
        std::vector<std::shared_ptr<tgfx::GPUBuffer>> buffers = {};
        size_t bufferIndex = 0;
        size_t cursor = 0;
    };

    uint32_t contextID_ = 0;
    tgfx::Context *context_ = nullptr;
    std::unordered_map<EntityId, std::shared_ptr<tgfx::RenderPipeline>> colorSourcePipelineMap_ = {};
    std::array<UniformBufferPacket, 3> uniformPackets_ = {};
    uint32_t uniformPacketIndex_ = 0;
    std::shared_ptr<tgfx::GPUBuffer> fullscreenVertexBuffer_ = nullptr;
};
};  // namespace motion

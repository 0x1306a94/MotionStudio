#pragma once

#include "effects/ColorSourceEffectResources.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
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
 * Per-context GPU cache for ColorSourceEffect: shared pipelines keyed by shader
 * fingerprint, and a triple-buffered bump allocator for uniform uploads so
 * Metal Shared storage is not rewritten while a prior frame still reads it.
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

    std::shared_ptr<tgfx::RenderPipeline> findColorSourcePipeline(const std::string &key) const;

    void addColorSourcePipeline(const std::string &key, std::shared_ptr<tgfx::RenderPipeline> pipeline);

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
    std::unordered_map<std::string, std::shared_ptr<tgfx::RenderPipeline>> colorSourcePipelineMap_ = {};
    std::array<UniformBufferPacket, 3> uniformPackets_ = {};
    uint32_t uniformPacketIndex_ = 0;
    std::shared_ptr<tgfx::GPUBuffer> fullscreenVertexBuffer_ = nullptr;
};
};  // namespace motion

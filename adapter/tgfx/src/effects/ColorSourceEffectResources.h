#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace tgfx {
class RenderPipeline;
class GPUBuffer;
class GPU;
};  // namespace tgfx

namespace motion {

/**
 * GPU resources for one ColorSourceEffect id. Uniform buffers are triple-buffered:
 * Metal Shared storage would race if the CPU rewrote a buffer still in flight, so
 * each onDraw rotates to the next slot (same strategy as tgfx GlobalCache UBO).
 */
struct ColorSourceEffectResource {
    static constexpr uint32_t UNIFORM_BUFFER_COUNT = 3;

    std::shared_ptr<tgfx::RenderPipeline> pipeline = nullptr;
    std::array<std::shared_ptr<tgfx::GPUBuffer>, UNIFORM_BUFFER_COUNT> uniformBuffers = {};
    uint32_t uniformBufferIndex = 0;

    // Returns the next ring slot for CPU write+GPU bind; creates it lazily.
    std::shared_ptr<tgfx::GPUBuffer> acquireUniformBuffer(tgfx::GPU *gpu, size_t size);

    virtual ~ColorSourceEffectResource() = default;
};

};  // namespace motion

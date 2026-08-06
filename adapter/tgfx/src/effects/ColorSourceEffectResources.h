#pragma once

#include <cstddef>
#include <memory>

namespace tgfx {
class GPUBuffer;
};  // namespace tgfx

namespace motion {

/**
 * One bump allocation from RenderCache's triple-buffered uniform pool.
 * Bind with setUniformBuffer(binding, buffer, offset, size).
 */
struct UniformBufferSlice {
    std::shared_ptr<tgfx::GPUBuffer> buffer = nullptr;
    size_t offset = 0;
};

};  // namespace motion

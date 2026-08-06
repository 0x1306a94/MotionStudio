#include "ColorSourceEffectResources.h"

#include <tgfx/gpu/GPU.h>
#include <tgfx/gpu/GPUBuffer.h>

namespace motion {

std::shared_ptr<tgfx::GPUBuffer> ColorSourceEffectResource::acquireUniformBuffer(tgfx::GPU *gpu, size_t size) {
    if (gpu == nullptr || size == 0) {
        return nullptr;
    }

    auto &slot = uniformBuffers[uniformBufferIndex];
    if (slot == nullptr) {
        slot = gpu->createBuffer(size, tgfx::GPUBufferUsage::UNIFORM);
        if (slot == nullptr) {
            return nullptr;
        }
    }

    auto buffer = slot;
    uniformBufferIndex = (uniformBufferIndex + 1) % UNIFORM_BUFFER_COUNT;
    return buffer;
}

};  // namespace motion

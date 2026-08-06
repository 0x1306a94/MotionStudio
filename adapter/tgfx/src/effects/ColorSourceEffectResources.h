#pragma once

#include <memory>

namespace tgfx {
class RenderPipeline;
class GPUBuffer;
};  // namespace tgfx

namespace motion {

struct ColorSourceEffectResource {
    std::shared_ptr<tgfx::RenderPipeline> pipeline = nullptr;
    std::shared_ptr<tgfx::GPUBuffer> uniformBuffer = nullptr;

    virtual ~ColorSourceEffectResource() = default;
};

};  // namespace motion

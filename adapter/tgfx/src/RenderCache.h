#pragma once

#include "effects/ColorSourceEffectResources.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <MotionStudio/common/EntityId.h>

namespace tgfx {
class RenderPipeline;
class GPUBuffer;
class Context;
class GPU;
};  // namespace tgfx

namespace motion {

class RenderCache {

  public:
    explicit RenderCache();
    ~RenderCache();

    void attachToContext(tgfx::Context *current, bool forDrawing = true);

    void detachFromContext();

    tgfx::Context *getContext() const {
        return context_;
    }

    ColorSourceEffectResource *findColorSourceEffectResource(EntityId ID);

    void addColorSourceEffectResource(EntityId ID, std::unique_ptr<ColorSourceEffectResource> resources);

    void setMainImageSource(EntityId ID, std::string source);

    const std::string *findMainImageSource(EntityId ID) const;

    // Shared clip-space fullscreen triangle VBO for the current context. Created lazily.
    std::shared_ptr<tgfx::GPUBuffer> getFullscreenVertexBuffer(tgfx::GPU *gpu);

    void releaseAll();

  private:
    uint32_t contextID_ = 0;
    tgfx::Context *context_ = nullptr;
    std::unordered_map<EntityId, std::unique_ptr<ColorSourceEffectResource>> colorSourceEffectResourceMap_ = {};
    std::unordered_map<EntityId, std::string> mainImageSourceMap_ = {};
    std::shared_ptr<tgfx::GPUBuffer> fullscreenVertexBuffer_ = nullptr;
};
};  // namespace motion

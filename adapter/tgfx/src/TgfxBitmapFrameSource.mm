#include "TgfxBitmapFrameSource.h"

#include <algorithm>
#include <unordered_set>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/EvaluatedLayer.h"
#include "MotionStudio/render/RenderAdapter.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/render/SceneState.h"
#include "TgfxRenderAdapter.h"

namespace motion {
namespace {

enum class BitmapRenderMode {
    None,
    Composition,
    Layer,
};

void CollectSubtreeIds(const Composition &host, EntityId root,
                       std::unordered_set<uint64_t> *ids) {
    ids->insert(root.value);
    bool added = true;
    while (added) {
        added = false;
        for (const auto &layer : host.layers) {
            if (layer == nullptr || ids->count(layer->id.value) != 0) {
                continue;
            }
            if (layer->parentId.isValid() && ids->count(layer->parentId.value) != 0) {
                ids->insert(layer->id.value);
                added = true;
            }
        }
    }
}

}  // namespace

struct TgfxBitmapFrameSource::Impl {
    BitmapRenderMode mode = BitmapRenderMode::None;
    const Document *document = nullptr;
    EntityId compositionId;
    TimeRange visibleRange = {};
    int pixelWidth = 0;
    int pixelHeight = 0;
    std::unordered_set<uint64_t> layerIds;
    std::unique_ptr<TgfxRenderAdapter> adapter;
    std::vector<uint8_t> pixels;
};

TgfxBitmapFrameSource::TgfxBitmapFrameSource()
    : impl_(std::make_unique<Impl>()) {
}

TgfxBitmapFrameSource::~TgfxBitmapFrameSource() {
    finish();
}

Expected<void, std::string> TgfxBitmapFrameSource::prepare(const Document &document,
                                                           EntityId hostCompositionId,
                                                           EntityId rootLayerId,
                                                           TimeRange visibleRange, int pixelWidth,
                                                           int pixelHeight) {
    finish();
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return Unexpected(std::string("invalid bitmap size"));
    }
    const Composition *host = document.entityIndex().findComposition(hostCompositionId);
    if (host == nullptr) {
        return Unexpected(std::string("composition not found"));
    }
    bool foundRoot = false;
    for (const auto &layer : host->layers) {
        if (layer != nullptr && layer->id == rootLayerId) {
            foundRoot = true;
            break;
        }
    }
    if (!foundRoot) {
        return Unexpected(std::string("root layer not found in host composition"));
    }

    CollectSubtreeIds(*host, rootLayerId, &impl_->layerIds);
    auto adapter = TgfxRenderAdapter::Make(pixelWidth, pixelHeight);
    if (!adapter) {
        return Unexpected(std::string("Metal unavailable for bitmap frame source"));
    }

    impl_->mode = BitmapRenderMode::Layer;
    impl_->document = &document;
    impl_->compositionId = hostCompositionId;
    impl_->visibleRange = visibleRange;
    impl_->pixelWidth = pixelWidth;
    impl_->pixelHeight = pixelHeight;
    impl_->adapter = std::move(adapter);
    return Expected<void, std::string>();
}

Expected<void, std::string> TgfxBitmapFrameSource::prepareComposition(const Document &document,
                                                                      EntityId compositionId,
                                                                      TimeRange visibleRange,
                                                                      int pixelWidth,
                                                                      int pixelHeight) {
    finish();
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return Unexpected(std::string("invalid bitmap size"));
    }
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected(std::string("composition not found"));
    }

    auto adapter = TgfxRenderAdapter::Make(pixelWidth, pixelHeight);
    if (!adapter) {
        return Unexpected(std::string("Metal unavailable for bitmap frame source"));
    }

    impl_->mode = BitmapRenderMode::Composition;
    impl_->document = &document;
    impl_->compositionId = compositionId;
    impl_->visibleRange = visibleRange;
    impl_->pixelWidth = pixelWidth;
    impl_->pixelHeight = pixelHeight;
    impl_->adapter = std::move(adapter);
    return Expected<void, std::string>();
}

Expected<BitmapFrame, std::string> TgfxBitmapFrameSource::renderFrame(FrameTime time) {
    if (impl_->document == nullptr || impl_->adapter == nullptr ||
        impl_->mode == BitmapRenderMode::None) {
        return Unexpected(std::string("frame source not prepared"));
    }
    if (!impl_->visibleRange.contains(time)) {
        return Unexpected(std::string("time outside visible range"));
    }

    auto state = SceneEvaluator::Evaluate(*impl_->document, impl_->compositionId, time);
    if (!state.hasValue()) {
        return Unexpected(state.error());
    }

    SceneState rendered = std::move(*state);
    rendered.cornerRadius = 0.0f;
    if (impl_->mode == BitmapRenderMode::Layer) {
        rendered.layers.erase(std::remove_if(rendered.layers.begin(), rendered.layers.end(),
                                             [this](const EvaluatedLayer &layer) {
                                                 return impl_->layerIds.count(layer.id.value) == 0;
                                             }),
                              rendered.layers.end());
    }

    const Color background = impl_->mode == BitmapRenderMode::Layer
        ? Color{0.0f, 0.0f, 0.0f, 0.0f}
        : rendered.backgroundColor;

    impl_->adapter->beginFrame(impl_->pixelWidth, impl_->pixelHeight, background, 0.0f);

    const float scaleX =
        static_cast<float>(impl_->pixelWidth) / static_cast<float>(std::max(rendered.viewportWidth, 1));
    const float scaleY = static_cast<float>(impl_->pixelHeight) /
        static_cast<float>(std::max(rendered.viewportHeight, 1));
    const bool needsScale = scaleX != 1.0f || scaleY != 1.0f;
    if (needsScale) {
        impl_->adapter->save();
        impl_->adapter->concatTransform(Mat3::Scale(Vec2{scaleX, scaleY}));
    }
    impl_->adapter->setColorSourceFrameContext(rendered.timeSeconds, rendered.frameIndex,
                                               rendered.frameRate);
    PlayCommands(BuildCommands(rendered), *impl_->adapter);
    if (needsScale) {
        impl_->adapter->restore();
    }
    impl_->adapter->endFrame();

    if (!impl_->adapter->ReadPixels(impl_->pixels)) {
        return Unexpected(std::string("failed to read bitmap pixels"));
    }

    BitmapFrame frame;
    frame.width = impl_->pixelWidth;
    frame.height = impl_->pixelHeight;
    frame.rgba = impl_->pixels.data();
    frame.rowBytes = static_cast<size_t>(impl_->pixelWidth) * 4u;
    frame.premultiplied = true;
    return frame;
}

void TgfxBitmapFrameSource::finish() {
    impl_->adapter.reset();
    impl_->document = nullptr;
    impl_->mode = BitmapRenderMode::None;
    impl_->layerIds.clear();
    impl_->pixels.clear();
    impl_->pixelWidth = 0;
    impl_->pixelHeight = 0;
}

}  // namespace motion

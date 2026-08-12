#include "PreviewEnsure.h"

#include <memory>
#include <utility>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/render/SceneEvaluator.h"

#include "BridgeInternals.h"

namespace bridge {

motion::Expected<const motionstudio::PreviewSceneCache::Entry *, std::string> EnsurePreviewScene(
    MSDocument *handle, uint64_t compositionId, motion::PreviewTime time) {
    if (handle == nullptr || handle->document == nullptr) {
        return motion::Unexpected(std::string("document is null"));
    }
    handle->previewSceneCache.invalidateIfStale(compositionId, handle->contentRevision);
    if (const motionstudio::PreviewSceneCache::Entry *hit = handle->previewSceneCache.find(time)) {
        return hit;
    }
    auto evaluated =
        motion::SceneEvaluator::EvaluatePreview(*handle->document, motion::EntityId{compositionId}, time);
    if (!evaluated.hasValue()) {
        return motion::Unexpected(evaluated.error());
    }
    auto entry = std::make_unique<motionstudio::PreviewSceneCache::Entry>();
    entry->time = time;
    entry->state = std::move(evaluated).value();
    ResolvePointTextContainerSizes(entry->state);
    motionstudio::PreviewSceneCache::Entry *stored =
        handle->previewSceneCache.put(time, std::move(entry));
    if (stored == nullptr) {
        return motion::Unexpected(std::string("failed to cache scene state"));
    }
    return stored;
}

}  // namespace bridge

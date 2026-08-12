#pragma once

#include <cstdint>
#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"

#include "MSDocument.h"
#include "PreviewSceneCache.h"

namespace bridge {

// Caller must hold DocumentLock. Evaluates and caches SceneState (ResolvePointText
// applied). Does not BuildCommands.
motion::Expected<const motionstudio::PreviewSceneCache::Entry *, std::string> EnsurePreviewScene(
    MSDocument *handle, uint64_t compositionId, motion::PreviewTime time);

}  // namespace bridge

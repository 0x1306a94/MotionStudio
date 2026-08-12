#pragma once

#include <cstdint>
#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

#include "MSCanvas.h"
#include "MSDocument.h"
#include "PreviewSceneCache.h"

namespace bridge {

// Caller must hold DocumentLock. Evaluates and caches SceneState (ResolvePointText
// applied). Does not BuildCommands.
motion::Expected<const motionstudio::PreviewSceneCache::Entry *, std::string> EnsurePreviewScene(
    MSDocument *handle, uint64_t compositionId, motion::PreviewTime time);

// Caller must hold DocumentLock. Lazily builds/caches scene DrawCommandList for draw.
const motion::DrawCommandList *EnsureSceneCommands(MSCanvas *canvas, MSDocument *handle,
                                                   uint64_t compositionId, motion::PreviewTime time,
                                                   const motion::SceneState &state);

}  // namespace bridge

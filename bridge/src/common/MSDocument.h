//
//  MSDocument.h
//  MotionStudio
//
//  Created by king on 2026/7/24.
//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "PreviewSceneCache.h"

namespace motion {
class Document;
class UndoManager;
};  // namespace motion

// The document model is only safe for single-threaded use, but callers reach
// it from multiple threads: UI edits on the main actor and document
// serialization on a background actor (ReferenceFileDocument snapshots run
// off the main actor). The mutex serializes every entry point.
struct MSDocument {
    std::mutex mutex;
    std::unique_ptr<motion::Document> document;
    std::unique_ptr<motion::UndoManager> undoManager;
    // Content generation from the app (Swift revision). Invalidates
    // PreviewSceneCache / FrameCommandCache when it changes.
    uint64_t contentRevision = 0;
    motionstudio::PreviewSceneCache previewSceneCache = {};
};

#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/ImageScaleMode.h"

namespace motion {

// Evaluated image payload for one visible image layer at a point in time.
struct EvaluatedImageItem {
    EntityId assetId;
    // Absolute filesystem path; empty when unbound or projectRoot/path missing.
    std::string absolutePath;
    Vec2 containerSize;
    Vec2 intrinsicSize;
    ImageScaleMode scaleMode = ImageScaleMode::LetterBox;
};

}  // namespace motion

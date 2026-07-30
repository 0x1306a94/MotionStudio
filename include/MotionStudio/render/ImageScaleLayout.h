#pragma once

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/ImageScaleMode.h"

namespace motion {

// Axis-aligned rectangle in layer-local space.
struct ImageRect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    bool isEmpty() const {
        return width <= 0.0f || height <= 0.0f;
    }
};

// Maps intrinsic image pixels into the container [0,0]–[cw,ch] per scale mode.
// Empty when container or intrinsic size is non-positive.
ImageRect ComputeImageDestinationRect(Vec2 containerSize, Vec2 intrinsicSize,
                                      ImageScaleMode mode);

}  // namespace motion

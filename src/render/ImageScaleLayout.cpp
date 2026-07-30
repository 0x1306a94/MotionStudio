#include "MotionStudio/render/ImageScaleLayout.h"

#include <algorithm>

namespace motion {

ImageRect ComputeImageDestinationRect(Vec2 containerSize, Vec2 intrinsicSize,
                                      ImageScaleMode mode) {
    ImageRect empty;
    if (containerSize.x <= 0.0f || containerSize.y <= 0.0f || intrinsicSize.x <= 0.0f ||
        intrinsicSize.y <= 0.0f) {
        return empty;
    }

    switch (mode) {
        case ImageScaleMode::None: {
            return ImageRect{0.0f, 0.0f, intrinsicSize.x, intrinsicSize.y};
        }
        case ImageScaleMode::Stretch: {
            return ImageRect{0.0f, 0.0f, containerSize.x, containerSize.y};
        }
        case ImageScaleMode::LetterBox: {
            const float scale = std::min(containerSize.x / intrinsicSize.x, containerSize.y / intrinsicSize.y);
            const float width = intrinsicSize.x * scale;
            const float height = intrinsicSize.y * scale;
            return ImageRect{(containerSize.x - width) * 0.5f, (containerSize.y - height) * 0.5f, width, height};
        }
        case ImageScaleMode::Zoom: {
            const float scale = std::max(containerSize.x / intrinsicSize.x, containerSize.y / intrinsicSize.y);
            const float width = intrinsicSize.x * scale;
            const float height = intrinsicSize.y * scale;
            return ImageRect{(containerSize.x - width) * 0.5f, (containerSize.y - height) * 0.5f, width, height};
        }
    }
    return empty;
}

}  // namespace motion

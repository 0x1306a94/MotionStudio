#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/render/EvaluatedTextItem.h"

namespace motion {

// Parameters for RenderAdapter::drawText. Path fields are inactive when
// textPathEnabled is false or textPath has no vertices.
struct TextDrawParams {
    std::string text;
    float fontSize = 48;
    Vec2 containerSize;
    bool boxTextMode = false;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    std::string fontStyle;
    std::vector<TextDrawStyle> styles;

    bool textPathEnabled = false;
    BezierPath textPath;
    bool textPathReversed = false;
    bool textPathPerpendicular = true;
    bool textPathForceAlignment = false;
    float textPathFirstMargin = 0;
    float textPathLastMargin = 0;
};

}  // namespace motion

#pragma once

#include <string>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextPath.h"

namespace motion {

// Layer content for editable text (point text or PAG-style box text).
class TextContent : public LayerContent {
  public:
    TextContent();
    ~TextContent() override;

    Animatable<std::string> text{std::string{"Text"}};
    // System font family name (must be installed on the device), e.g. "Fira Code".
    std::string fontFamily{"PingFang SC"};
    // System style name within the family (e.g. "Bold"); empty = default/Regular.
    std::string fontStyle{};
    // Font size cap; shrink applies only when boxTextMode is true.
    float fontSize = 48.0f;
    // Layout box for box text; point text ignores this for layout/selection.
    Vec2 size{400, 120};
    // true: PAG box text (wrap + shrink); false: point text.
    bool boxTextMode = false;
    TextAlign align = TextAlign::Left;
    // Path layout for point text; ignored when invalid / unbound.
    TextPath textPath;
};

}  // namespace motion

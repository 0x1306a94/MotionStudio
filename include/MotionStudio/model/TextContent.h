#pragma once

#include <string>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/LayerContent.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// Layer content for editable boxed text with animatable string and size.
class TextContent : public LayerContent {
  public:
    TextContent();
    ~TextContent() override;

    Animatable<std::string> text{std::string{"Text"}};
    // System font family name (must be installed on the device), e.g. "Fira Code".
    std::string fontFamily{"PingFang SC"};
    // System style name within the family (e.g. "Bold"); empty = default/Regular.
    std::string fontStyle{};
    // Font size cap; shrink may apply when autoHeight is false.
    Animatable<float> fontSize{48.0f};
    // Virtual container; width always constrains wrapping.
    Animatable<Vec2> size{Vec2{400, 120}};
    // true: height follows content; false: fixed height with font shrink.
    bool autoHeight = true;
    TextAlign align = TextAlign::Left;
};

}  // namespace motion

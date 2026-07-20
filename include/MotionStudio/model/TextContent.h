#pragma once

#include <string>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/LayerContent.h"

namespace motion {

// Layer content for editable text with animatable string and size.
class TextContent : public LayerContent {
  public:
    TextContent();
    ~TextContent() override;

    Animatable<std::string> text{std::string{}};
    std::string fontFamily;
    Animatable<float> fontSize{24.0f};
};

}  // namespace motion

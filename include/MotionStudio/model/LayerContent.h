#pragma once

#include "MotionStudio/model/LayerType.h"

namespace motion {

// Polymorphic base for the five layer content variants
// (Shape / Image / Text / Null / Precomp).
class LayerContent {
  public:
    // type: which content variant this instance represents.
    explicit LayerContent(LayerType type);
    virtual ~LayerContent();

    // Returns the content variant tag.
    LayerType type() const;

  private:
    LayerType type_;
};

}  // namespace motion

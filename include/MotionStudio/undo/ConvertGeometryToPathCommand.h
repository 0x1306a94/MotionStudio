#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/ShapeElement.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Bakes a layer's Rect/Ellipse geometry into a ShapePath at frame. Already-Path
// geometry is a no-op. Undo restores the previous parametric geometry.
class ConvertGeometryToPathCommand : public Command {
  public:
    // layerId: shape layer whose geometry should be converted.
    // frame: playhead frame used to evaluate animated Rect/Ellipse properties.
    ConvertGeometryToPathCommand(EntityId layerId, FrameTime frame);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    FrameTime frame_ = 0;
    // Geometry not currently on the layer (Rect after convert, Path after undo).
    std::unique_ptr<ShapeElement> stashed_;
    bool didConvert_ = false;
};

}  // namespace motion

#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetImageScaleModeCommand : public Command {
  public:
    SetImageScaleModeCommand(EntityId layerId, ImageScaleMode mode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    ImageScaleMode mode_;
    std::optional<ImageScaleMode> oldMode_;
};

}  // namespace motion

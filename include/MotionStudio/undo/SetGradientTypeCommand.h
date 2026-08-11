#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetGradientTypeCommand : public Command {
  public:
    SetGradientTypeCommand(EntityId layerId, int styleIndex, GradientType type);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int styleIndex_ = -1;
    GradientType type_ = GradientType::Linear;
    std::optional<GradientType> oldType_;
};

}  // namespace motion

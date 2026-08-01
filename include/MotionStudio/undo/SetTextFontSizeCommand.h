#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets TextContent::fontSize (static).
class SetTextFontSizeCommand : public Command {
  public:
    SetTextFontSizeCommand(EntityId layerId, float fontSize);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    float fontSize_ = 48.0f;
    std::optional<float> oldFontSize_;
};

}  // namespace motion

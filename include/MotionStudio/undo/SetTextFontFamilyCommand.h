#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetTextFontFamilyCommand : public Command {
  public:
    SetTextFontFamilyCommand(EntityId layerId, std::string fontFamily);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    std::string fontFamily_;
    std::optional<std::string> oldFontFamily_;
};

}  // namespace motion

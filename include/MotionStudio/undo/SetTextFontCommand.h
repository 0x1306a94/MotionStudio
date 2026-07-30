#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetTextFontCommand : public Command {
  public:
    // layerId: text layer to update.
    // fontFamily: system font family name (e.g. "Fira Code").
    // fontStyle: system style name (e.g. "Bold"); empty means default/Regular traits.
    SetTextFontCommand(EntityId layerId, std::string fontFamily, std::string fontStyle);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    std::string fontFamily_;
    std::string fontStyle_;
    std::optional<std::string> oldFontFamily_;
    std::optional<std::string> oldFontStyle_;
};

}  // namespace motion

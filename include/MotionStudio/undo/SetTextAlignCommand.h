#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetTextAlignCommand : public Command {
  public:
    SetTextAlignCommand(EntityId layerId, TextAlign align);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    TextAlign align_ = TextAlign::Left;
    std::optional<TextAlign> oldAlign_;
};

}  // namespace motion

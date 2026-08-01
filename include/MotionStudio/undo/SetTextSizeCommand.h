#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets TextContent::size (static layout box).
class SetTextSizeCommand : public Command {
  public:
    SetTextSizeCommand(EntityId layerId, Vec2 size);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    Vec2 size_{400, 120};
    std::optional<Vec2> oldSize_;
};

}  // namespace motion

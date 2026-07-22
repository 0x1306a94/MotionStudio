#include "MotionStudio/undo/SetCompositionCornerRadiusCommand.h"

#include <algorithm>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/undo/SetCompositionSettingsCommand.h"

namespace motion {

SetCompositionCornerRadiusCommand::SetCompositionCornerRadiusCommand(EntityId compositionId,
                                                                     float cornerRadius)
    : compositionId_(compositionId)
    , cornerRadius_(std::max(cornerRadius, 0.0f)) {
}

void SetCompositionCornerRadiusCommand::execute(Document &document) {
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition == nullptr) {
        return;
    }
    if (!oldCornerRadius_) {
        oldCornerRadius_ = composition->cornerRadius;
    }
    composition->cornerRadius = std::max(cornerRadius_, 0.0f);
}

void SetCompositionCornerRadiusCommand::undo(Document &document) {
    if (!oldCornerRadius_) {
        return;
    }
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition != nullptr) {
        composition->cornerRadius = *oldCornerRadius_;
    }
}

bool SetCompositionCornerRadiusCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetCompositionCornerRadius) {
        return false;
    }
    const auto &typed = static_cast<const SetCompositionCornerRadiusCommand &>(other);
    if (typed.compositionId_ != compositionId_) {
        return false;
    }
    cornerRadius_ = typed.cornerRadius_;
    return true;
}

CommandKind SetCompositionCornerRadiusCommand::kind() const {
    return CommandKind::SetCompositionCornerRadius;
}

std::string SetCompositionCornerRadiusCommand::describe() const {
    return "Set Composition Corner Radius";
}

namespace {

CompositionSettings SanitizeSettings(CompositionSettings settings) {
    settings.width = std::max(settings.width, 1);
    settings.height = std::max(settings.height, 1);
    settings.duration = std::max(settings.duration, FrameTime(1));
    settings.frameRate.num = std::max(settings.frameRate.num, uint32_t(1));
    settings.frameRate.den = std::max(settings.frameRate.den, uint32_t(1));
    return settings;
}

CompositionSettings SettingsOf(const Composition &composition) {
    CompositionSettings settings;
    settings.width = composition.width;
    settings.height = composition.height;
    settings.duration = composition.duration;
    settings.frameRate = composition.frameRate;
    return settings;
}

void ApplySettings(Composition &composition, CompositionSettings settings) {
    settings = SanitizeSettings(settings);
    composition.width = settings.width;
    composition.height = settings.height;
    composition.duration = settings.duration;
    composition.frameRate = settings.frameRate;
}

}  // namespace

SetCompositionSettingsCommand::SetCompositionSettingsCommand(EntityId compositionId,
                                                             CompositionSettings settings)
    : compositionId_(compositionId)
    , settings_(SanitizeSettings(settings)) {
}

void SetCompositionSettingsCommand::execute(Document &document) {
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition == nullptr) {
        return;
    }
    if (!oldSettings_) {
        oldSettings_ = SettingsOf(*composition);
    }
    ApplySettings(*composition, settings_);
}

void SetCompositionSettingsCommand::undo(Document &document) {
    if (!oldSettings_) {
        return;
    }
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition != nullptr) {
        ApplySettings(*composition, *oldSettings_);
    }
}

bool SetCompositionSettingsCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetCompositionSettings) {
        return false;
    }
    const auto &typed = static_cast<const SetCompositionSettingsCommand &>(other);
    if (typed.compositionId_ != compositionId_) {
        return false;
    }
    settings_ = typed.settings_;
    return true;
}

CommandKind SetCompositionSettingsCommand::kind() const {
    return CommandKind::SetCompositionSettings;
}

std::string SetCompositionSettingsCommand::describe() const {
    return "Set Composition Settings";
}

}  // namespace motion

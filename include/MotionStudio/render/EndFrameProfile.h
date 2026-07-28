#pragma once

namespace motion {

// Timing breakdown for RenderAdapter::endFrame. Values are milliseconds;
// backends that do not profile a phase leave it at zero.
struct EndFrameProfile {
    double canvasRestoreMs = 0;
    double presentTargetMs = 0;
    double flushSubmitMs = 0;
    double deviceUnlockMs = 0;
};

}  // namespace motion

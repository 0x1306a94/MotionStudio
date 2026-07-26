#pragma once

namespace motion {

// How BeginMask..EndMask coverage is interpreted when applied to the current
// offscreen layer.
enum class MaskApplyMode {
    PathCoverage,
    AlphaMatte,
    AlphaMatteInverted,
    LumaMatte,
    LumaMatteInverted,
};

}  // namespace motion

#pragma once

namespace motion {

// How another layer's pixels contribute as a track matte for this layer.
enum class TrackMatteType {
    None,
    Alpha,
    AlphaInverted,
    Luma,
    LumaInverted,
};

}  // namespace motion

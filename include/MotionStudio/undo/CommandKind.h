#pragma once

namespace motion {

// Identifies the concrete command type so mergeWith can type-check the other
// command before downcasting (dynamic_cast is banned by the coding rules).
enum class CommandKind {
    AddLayer,
    RemoveLayer,
    MoveLayer,
    SetStaticValue,
    AddKeyframe,
    RemoveKeyframe,
    MoveKeyframe,
    SetEasing,
    SetLayerVisible,
    SetLayerLocked,
    SetCompositionBackgroundColor,
    SetCompositionCornerRadius,
    SetCompositionSettings,
    AddLayerStyle,
    RemoveStyle,
    SetStyleBlendMode,
    SetLayerBlendMode,
    SetStrokePosition,
    AddMask,
    RemoveMask,
    MoveMask,
    SetMaskMode,
    SetMaskInverted,
    SetTrackMatte,
    ConvertGeometryToPath,
    SetSpatialTangents,
    SetFollowPath,
    ImportImageAsset,
    SetImageAsset,
    SetImageScaleMode,
    Composite,
};

}  // namespace motion

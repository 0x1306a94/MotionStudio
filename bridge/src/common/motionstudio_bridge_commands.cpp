#include "motionstudio_bridge.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/common/Vec4.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/AddLayerEffectCommand.h"
#include "MotionStudio/undo/AddLayerFxCommand.h"
#include "MotionStudio/undo/AddLayerStyleCommand.h"
#include "MotionStudio/undo/AddMaskCommand.h"
#include "MotionStudio/undo/ConvertGeometryToPathCommand.h"
#include "MotionStudio/undo/MoveKeyframeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/MoveLayerEffectCommand.h"
#include "MotionStudio/undo/MoveLayerFxCommand.h"
#include "MotionStudio/undo/MoveLayerStyleCommand.h"
#include "MotionStudio/undo/MoveMaskCommand.h"
#include "MotionStudio/undo/RemoveKeyframeCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/RemoveLayerEffectCommand.h"
#include "MotionStudio/undo/RemoveLayerFxCommand.h"
#include "MotionStudio/undo/RemoveMaskCommand.h"
#include "MotionStudio/undo/RemoveStyleCommand.h"
#include "MotionStudio/undo/SetCompositionBackgroundColorCommand.h"
#include "MotionStudio/undo/SetCompositionCornerRadiusCommand.h"
#include "MotionStudio/undo/SetCompositionSettingsCommand.h"
#include "MotionStudio/undo/SetEasingCommand.h"
#include "MotionStudio/undo/SetFollowPathCommand.h"
#include "MotionStudio/undo/SetGaussianBlurRepeatEdgeCommand.h"
#include "MotionStudio/undo/SetLayerBlendModeCommand.h"
#include "MotionStudio/undo/SetLayerEffectEnabledCommand.h"
#include "MotionStudio/undo/SetLayerFxBlendModeCommand.h"
#include "MotionStudio/undo/SetLayerFxEnabledCommand.h"
#include "MotionStudio/undo/SetLayerFxStrokePositionCommand.h"
#include "MotionStudio/undo/SetLayerLockedCommand.h"
#include "MotionStudio/undo/SetLayerNameCommand.h"
#include "MotionStudio/undo/SetLayerVisibleCommand.h"
#include "MotionStudio/undo/SetMaskInvertedCommand.h"
#include "MotionStudio/undo/SetMaskModeCommand.h"
#include "MotionStudio/undo/SetSpatialTangentsCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/SetStrokePositionCommand.h"
#include "MotionStudio/undo/SetStyleBlendModeCommand.h"
#include "MotionStudio/undo/SetTrackMatteCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Color;
using motion::Composition;
using motion::EntityId;
using motion::FrameTime;
using motion::Layer;
using motion::Vec2;
using motion::Vec3;
using motion::Vec4;

/* ============================ commands ============================ */

void ms_command_set_static_float(MSDocument *document, uint64_t entityId, const char *path, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(value)));
}

void ms_command_set_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Vec2{x, y})));
}

void ms_command_set_static_vec3(MSDocument *document, uint64_t entityId, const char *path, float x, float y, float z) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Vec3{x, y, z})));
}

void ms_command_set_static_vec4(MSDocument *document, uint64_t entityId, const char *path, float x, float y, float z,
                                float w) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Vec4{x, y, z, w})));
}

void ms_command_set_static_color(MSDocument *document, uint64_t entityId, const char *path, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Color{r, g, b, a})));
}

void ms_command_set_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                       const MSBezierPath *value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(BridgeNetworkFromPath(FromMSBezierPath(value)))));
}

void ms_command_set_static_vector_network(MSDocument *document, uint64_t entityId, const char *path,
                                          const MSVectorNetwork *value) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::SetStaticValueCommand>(
                MakePath(entityId, path), motion::PropertyValue(FromMSVectorNetwork(value))));
}

void ms_command_set_static_string(MSDocument *document, uint64_t entityId, const char *path,
                                  const char *value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(value == nullptr ? std::string{} : std::string(value))));
}

void ms_command_set_composition_background_color(MSDocument *document, uint64_t compositionId, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionBackgroundColorCommand>(EntityId{compositionId}, Color{r, g, b, a}));
}

void ms_command_set_composition_corner_radius(MSDocument *document, uint64_t compositionId, float cornerRadius) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionCornerRadiusCommand>(EntityId{compositionId}, cornerRadius));
}

void ms_command_set_composition_size(MSDocument *document, uint64_t compositionId, int width, int height) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = width;
    settings.height = height;
    settings.duration = composition->duration;
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_duration(MSDocument *document, uint64_t compositionId, int64_t duration) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = static_cast<FrameTime>(duration);
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_frame_rate(MSDocument *document, uint64_t compositionId, int frameRateNum, int frameRateDen) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || frameRateNum <= 0 || frameRateDen <= 0) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = composition->duration;
    settings.frameRate = {static_cast<uint32_t>(frameRateNum),
                          static_cast<uint32_t>(frameRateDen)};
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), value))));
}

void ms_command_add_keyframe_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Vec2{x, y}))));
}

void ms_command_add_keyframe_vec3(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x,
                                  float y, float z) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Vec3{x, y, z}))));
}

void ms_command_add_keyframe_vec4(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x,
                                  float y, float z, float w) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::AddKeyframeCommand>(
                MakePath(entityId, path),
                motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Vec4{x, y, z, w}))));
}

void ms_command_add_keyframe_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), Color{r, g, b, a}))));
}

void ms_command_add_keyframe_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                         int64_t frame, const MSBezierPath *value) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::AddKeyframeCommand>(
                MakePath(entityId, path),
                motion::KeyframeData(
                    MakeKeyframe(static_cast<FrameTime>(frame), BridgeNetworkFromPath(FromMSBezierPath(value))))));
}

void ms_command_add_keyframe_vector_network(MSDocument *document, uint64_t entityId, const char *path,
                                            int64_t frame, const MSVectorNetwork *value) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::AddKeyframeCommand>(
                MakePath(entityId, path),
                motion::KeyframeData(
                    MakeKeyframe(static_cast<FrameTime>(frame), FromMSVectorNetwork(value)))));
}

void ms_command_write_bezier_path_at_playhead(MSDocument *document, uint64_t entityId,
                                              const char *path, int64_t frame,
                                              const MSBezierPath *value) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr || property->valueType() != AnimatableType::VectorNetwork) {
        return;
    }
    const bool animated = static_cast<Animatable<motion::VectorNetwork> *>(property)->isAnimated();
    if (animated) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(
                        MakeKeyframe(static_cast<FrameTime>(frame), BridgeNetworkFromPath(FromMSBezierPath(value))))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(BridgeNetworkFromPath(FromMSBezierPath(value)))));
}

void ms_command_write_vector_network_at_playhead(MSDocument *document, uint64_t entityId,
                                                 const char *path, int64_t frame,
                                                 const MSVectorNetwork *value) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr || property->valueType() != AnimatableType::VectorNetwork) {
        return;
    }
    const motion::VectorNetwork network = FromMSVectorNetwork(value);
    if (static_cast<Animatable<motion::VectorNetwork> *>(property)->isAnimated()) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(MakeKeyframe(static_cast<FrameTime>(frame), network))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(network)));
}

void ms_command_remove_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveKeyframeCommand>(MakePath(entityId, path), static_cast<FrameTime>(frame)));
}

void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t oldFrame, int64_t newFrame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveKeyframeCommand>(MakePath(entityId, path), static_cast<FrameTime>(oldFrame), static_cast<FrameTime>(newFrame)));
}

void ms_command_set_easing(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, int easingType, float inX, float inY, float outX, float outY) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetEasingCommand>(MakePath(entityId, path), static_cast<FrameTime>(frame), MakeEasing(easingType, inX, inY, outX, outY)));
}

void ms_command_set_spatial_tangents(MSDocument *document, uint64_t entityId, const char *path,
                                     int64_t frame, bool hasIn, float inX, float inY, bool hasOut,
                                     float outX, float outY) {
    DocumentLock guard(document);
    std::optional<Vec2> spatialIn;
    std::optional<Vec2> spatialOut;
    if (hasIn) {
        spatialIn = Vec2{inX, inY};
    }
    if (hasOut) {
        spatialOut = Vec2{outX, outY};
    }
    Execute(document, std::make_unique<motion::SetSpatialTangentsCommand>(MakePath(entityId, path), static_cast<FrameTime>(frame), spatialIn, spatialOut));
}

uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, false);
}

uint64_t ms_command_add_ellipse_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, true);
}

uint64_t ms_command_add_path_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddPathLayer(document, compositionId);
}

void ms_command_convert_geometry_to_path(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::ConvertGeometryToPathCommand>(EntityId{layerId}, static_cast<FrameTime>(frame)));
}

void ms_command_remove_layer(MSDocument *document, uint64_t compositionId, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveLayerCommand>(EntityId{compositionId}, EntityId{layerId}));
}

void ms_command_move_layer(MSDocument *document, uint64_t compositionId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerCommand>(EntityId{compositionId}, fromIndex, toIndex));
}

void ms_command_set_layer_visible(MSDocument *document, uint64_t layerId, bool visible) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerVisibleCommand>(EntityId{layerId}, visible));
}

void ms_command_set_layer_locked(MSDocument *document, uint64_t layerId, bool locked) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerLockedCommand>(EntityId{layerId}, locked));
}

void ms_command_set_layer_name(MSDocument *document, uint64_t layerId, const char *name) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerNameCommand>(EntityId{layerId}, name != nullptr ? name : ""));
}

void ms_command_set_layer_blend_mode(MSDocument *document, uint64_t layerId, MS_BLEND blendMode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerBlendModeCommand>(EntityId{layerId}, MakeBlendMode(blendMode)));
}

void ms_command_add_fill_style(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerStyleCommand>(EntityId{layerId}, std::make_unique<motion::FillStyle>()));
}

void ms_command_add_stroke_style(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerStyleCommand>(EntityId{layerId}, std::make_unique<motion::StrokeStyle>()));
}

void ms_command_remove_style(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveStyleCommand>(EntityId{layerId}, index));
}

void ms_command_move_layer_style(MSDocument *document, uint64_t layerId, int fromIndex,
                                 int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerStyleCommand>(EntityId{layerId}, fromIndex, toIndex));
}

void ms_command_set_style_blend_mode(MSDocument *document, uint64_t layerId, int index, MS_BLEND blendMode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStyleBlendModeCommand>(EntityId{layerId}, index, MakeBlendMode(blendMode)));
}

void ms_command_set_stroke_position(MSDocument *document, uint64_t layerId, int index, MS_STROKE_POSITION position) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStrokePositionCommand>(EntityId{layerId}, index, MakeStrokePosition(position)));
}

void ms_command_add_brightness_contrast_effect(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerEffectCommand>(EntityId{layerId}, std::make_unique<motion::BrightnessContrastEffect>()));
}

void ms_command_add_gaussian_blur_effect(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerEffectCommand>(EntityId{layerId}, std::make_unique<motion::GaussianBlurEffect>()));
}

void ms_command_remove_layer_effect(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveLayerEffectCommand>(EntityId{layerId}, index));
}

void ms_command_move_layer_effect(MSDocument *document, uint64_t layerId, int fromIndex,
                                  int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerEffectCommand>(EntityId{layerId}, fromIndex, toIndex));
}

void ms_command_set_layer_effect_enabled(MSDocument *document, uint64_t layerId, int index,
                                         bool enabled) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerEffectEnabledCommand>(EntityId{layerId}, index, enabled));
}

void ms_command_set_gaussian_blur_repeat_edge(MSDocument *document, uint64_t layerId, int index,
                                              bool repeatEdgePixels) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetGaussianBlurRepeatEdgeCommand>(EntityId{layerId}, index, repeatEdgePixels));
}

void ms_command_add_layer_fx_drop_shadow(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerFxCommand>(EntityId{layerId}, std::make_unique<motion::DropShadowStyle>()));
}

void ms_command_add_layer_fx_outer_glow(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerFxCommand>(EntityId{layerId}, std::make_unique<motion::OuterGlowStyle>()));
}

void ms_command_add_layer_fx_stroke(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddLayerFxCommand>(EntityId{layerId}, std::make_unique<motion::LayerStrokeStyle>()));
}

void ms_command_remove_layer_fx(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveLayerFxCommand>(EntityId{layerId}, index));
}

void ms_command_move_layer_fx(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerFxCommand>(EntityId{layerId}, fromIndex, toIndex));
}

void ms_command_set_layer_fx_enabled(MSDocument *document, uint64_t layerId, int index, bool enabled) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerFxEnabledCommand>(EntityId{layerId}, index, enabled));
}

void ms_command_set_layer_fx_blend_mode(MSDocument *document, uint64_t layerId, int index, MS_BLEND blendMode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerFxBlendModeCommand>(EntityId{layerId}, index, MakeBlendMode(blendMode)));
}

void ms_command_set_layer_fx_stroke_position(MSDocument *document, uint64_t layerId, int index,
                                             MS_STROKE_POSITION position) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerFxStrokePositionCommand>(EntityId{layerId}, index, MakeStrokePosition(position)));
}

void ms_command_add_mask(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return;
    }
    Execute(document, std::make_unique<motion::AddMaskCommand>(EntityId{layerId}, MakeMaskFromLayer(*layer, frame)));
}

void ms_command_remove_mask(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveMaskCommand>(EntityId{layerId}, index));
}

void ms_command_move_mask(MSDocument *document, uint64_t layerId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveMaskCommand>(EntityId{layerId}, fromIndex, toIndex));
}

void ms_command_set_mask_mode(MSDocument *document, uint64_t layerId, int index, MS_MASK mode) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetMaskModeCommand>(EntityId{layerId}, index, MakeMaskMode(mode)));
}

void ms_command_set_mask_inverted(MSDocument *document, uint64_t layerId, int index,
                                  bool inverted) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetMaskInvertedCommand>(EntityId{layerId}, index, inverted));
}

void ms_command_set_track_matte(MSDocument *document, uint64_t layerId, uint64_t matteLayerId,
                                MS_TRACK_MATTE type) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetTrackMatteCommand>(EntityId{layerId}, EntityId{matteLayerId}, MakeTrackMatteType(type)));
}

void ms_command_set_follow_path(MSDocument *document, uint64_t layerId, bool enabled,
                                uint64_t pathLayerId, bool orientAlongPath) {
    DocumentLock guard(document);
    Execute(document,
            std::make_unique<motion::SetFollowPathCommand>(EntityId{layerId}, enabled,
                                                           EntityId{pathLayerId}, orientAlongPath));
}

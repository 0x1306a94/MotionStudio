#pragma once

#include <memory>
#include <string>
#include <utility>

#include "motionstudio_bridge.h"

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/undo/Command.h"

#include "MSDocument.h"

namespace bridge {

motion::Document *Doc(MSDocument *handle);
motion::Composition *FindComposition(MSDocument *handle, uint64_t compositionId);
motion::Layer *FindLayer(MSDocument *handle, uint64_t layerId);
motion::AnimatableBase *FindProperty(MSDocument *handle, uint64_t entityId, const char *path);

const motion::Animatable<float> *AsFloat(motion::AnimatableBase *base);
const motion::Animatable<motion::Vec2> *AsVec2(motion::AnimatableBase *base);
const motion::Animatable<motion::Color> *AsColor(motion::AnimatableBase *base);
const motion::Animatable<motion::BezierPath> *AsBezierPath(motion::AnimatableBase *base);

MSBezierPath *AllocateMSBezierPath(const motion::BezierPath &path);
motion::BezierPath FromMSBezierPath(const MSBezierPath *path);

template <typename T>
motion::Keyframe<T> MakeKeyframe(motion::FrameTime time, T value) {
    motion::Keyframe<T> keyframe;
    keyframe.time = time;
    keyframe.value = std::move(value);
    return keyframe;
}

void Execute(MSDocument *handle, std::unique_ptr<motion::Command> command);
motion::PropertyPath MakePath(uint64_t entityId, const char *path);

motion::BlendMode MakeBlendMode(MS_BLEND blendMode);
motion::StrokePosition MakeStrokePosition(MS_STROKE_POSITION position);
motion::MaskMode MakeMaskMode(MS_MASK mode);
motion::TrackMatteType MakeTrackMatteType(MS_TRACK_MATTE type);
motion::Mask MakeMaskFromLayer(const motion::Layer &layer, int64_t frame);
motion::Easing MakeEasing(int easingType, float inX, float inY, float outX, float outY);

uint64_t AddShapeLayer(MSDocument *handle, uint64_t compositionId, bool ellipse);
uint64_t AddPathLayer(MSDocument *handle, uint64_t compositionId);

}  // namespace bridge

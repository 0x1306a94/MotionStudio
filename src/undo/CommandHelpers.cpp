#include "CommandHelpers.h"

#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Composition.h"

namespace motion {

namespace {

template <typename T>
bool ApplyStaticValue(Animatable<T> *animatable, const PropertyValue &newValue,
                      PropertyValue &oldValue) {
    const T *typed = std::get_if<T>(&newValue);
    if (!typed) {
        return false;
    }
    oldValue = animatable->staticValue();
    animatable->setStaticValue(*typed);
    return true;
}

template <typename T>
std::optional<KeyframeData> TakeKeyframeTyped(Animatable<T> *animatable, FrameTime time) {
    auto keyframe = animatable->takeKeyframe(time);
    if (!keyframe) {
        return std::nullopt;
    }
    return KeyframeData{std::move(*keyframe)};
}

template <typename T>
bool ApplyEasing(Animatable<T> *animatable, FrameTime time, const Easing &easing,
                 Easing *oldEasingOut) {
    for (const Keyframe<T> &keyframe : animatable->keyframes()) {
        if (keyframe.time != time) {
            continue;
        }
        if (oldEasingOut) {
            *oldEasingOut = keyframe.easing;
        }
        Keyframe<T> updated = keyframe;
        updated.easing = easing;
        return animatable->updateKeyframe(time, std::move(updated));
    }
    return false;
}

}  // namespace

bool ApplyStaticValueAny(AnimatableBase *target, const PropertyValue &newValue,
                         PropertyValue &oldValue) {
    switch (target->valueType()) {
        case AnimatableType::Float: {
            return ApplyStaticValue(static_cast<Animatable<float> *>(target), newValue,
                                    oldValue);
        }
        case AnimatableType::Vec2: {
            return ApplyStaticValue(static_cast<Animatable<Vec2> *>(target), newValue,
                                    oldValue);
        }
        case AnimatableType::Vec3: {
            return false;
        }
        case AnimatableType::Color: {
            return ApplyStaticValue(static_cast<Animatable<Color> *>(target), newValue,
                                    oldValue);
        }
        case AnimatableType::BezierPath: {
            // Authoring properties use VectorNetwork; BezierPath Animatables are export-only.
            return false;
        }
        case AnimatableType::VectorNetwork: {
            return ApplyStaticValue(static_cast<Animatable<VectorNetwork> *>(target),
                                    newValue, oldValue);
        }
        case AnimatableType::String: {
            return ApplyStaticValue(static_cast<Animatable<std::string> *>(target),
                                    newValue, oldValue);
        }
    }
    return false;
}

std::optional<KeyframeData> TakeKeyframeAny(AnimatableBase *target, FrameTime time) {
    switch (target->valueType()) {
        case AnimatableType::Float: {
            return TakeKeyframeTyped(static_cast<Animatable<float> *>(target), time);
        }
        case AnimatableType::Vec2: {
            return TakeKeyframeTyped(static_cast<Animatable<Vec2> *>(target), time);
        }
        case AnimatableType::Vec3: {
            return std::nullopt;
        }
        case AnimatableType::Color: {
            return TakeKeyframeTyped(static_cast<Animatable<Color> *>(target), time);
        }
        case AnimatableType::BezierPath: {
            return std::nullopt;
        }
        case AnimatableType::VectorNetwork: {
            return TakeKeyframeTyped(static_cast<Animatable<VectorNetwork> *>(target), time);
        }
        case AnimatableType::String: {
            return TakeKeyframeTyped(static_cast<Animatable<std::string> *>(target), time);
        }
    }
    return std::nullopt;
}

void AddKeyframeAny(AnimatableBase *target, const KeyframeData &keyframe) {
    // Variant alternative order: float / Vec2 / Color / VectorNetwork / std::string.
    // Silently skip when keyframe type does not match property type.
    switch (keyframe.index()) {
        case 0: {
            if (target->valueType() == AnimatableType::Float) {
                static_cast<Animatable<float> *>(target)->addKeyframe(std::get<0>(keyframe));
            }
            break;
        }
        case 1: {
            if (target->valueType() == AnimatableType::Vec2) {
                static_cast<Animatable<Vec2> *>(target)->addKeyframe(std::get<1>(keyframe));
            }
            break;
        }
        case 2: {
            if (target->valueType() == AnimatableType::Color) {
                static_cast<Animatable<Color> *>(target)->addKeyframe(std::get<2>(keyframe));
            }
            break;
        }
        case 3: {
            if (target->valueType() == AnimatableType::VectorNetwork) {
                static_cast<Animatable<VectorNetwork> *>(target)->addKeyframe(
                    std::get<3>(keyframe));
            }
            break;
        }
        default: {
            if (target->valueType() == AnimatableType::String) {
                static_cast<Animatable<std::string> *>(target)->addKeyframe(
                    std::get<4>(keyframe));
            }
            break;
        }
    }
}

FrameTime KeyframeTime(const KeyframeData &keyframe) {
    switch (keyframe.index()) {
        case 0: {
            return std::get<0>(keyframe).time;
        }
        case 1: {
            return std::get<1>(keyframe).time;
        }
        case 2: {
            return std::get<2>(keyframe).time;
        }
        case 3: {
            return std::get<3>(keyframe).time;
        }
        default: {
            return std::get<4>(keyframe).time;
        }
    }
}

void SetKeyframeTime(KeyframeData &keyframe, FrameTime time) {
    switch (keyframe.index()) {
        case 0: {
            std::get<0>(keyframe).time = time;
            break;
        }
        case 1: {
            std::get<1>(keyframe).time = time;
            break;
        }
        case 2: {
            std::get<2>(keyframe).time = time;
            break;
        }
        case 3: {
            std::get<3>(keyframe).time = time;
            break;
        }
        default: {
            std::get<4>(keyframe).time = time;
            break;
        }
    }
}

bool ApplyEasingAny(AnimatableBase *target, FrameTime time, const Easing &easing,
                    Easing *oldEasingOut) {
    switch (target->valueType()) {
        case AnimatableType::Float: {
            return ApplyEasing(static_cast<Animatable<float> *>(target), time, easing,
                               oldEasingOut);
        }
        case AnimatableType::Vec2: {
            return ApplyEasing(static_cast<Animatable<Vec2> *>(target), time, easing,
                               oldEasingOut);
        }
        case AnimatableType::Vec3: {
            return false;
        }
        case AnimatableType::Color: {
            return ApplyEasing(static_cast<Animatable<Color> *>(target), time, easing,
                               oldEasingOut);
        }
        case AnimatableType::BezierPath: {
            return false;
        }
        case AnimatableType::VectorNetwork: {
            return ApplyEasing(static_cast<Animatable<VectorNetwork> *>(target), time,
                               easing, oldEasingOut);
        }
        case AnimatableType::String: {
            return ApplyEasing(static_cast<Animatable<std::string> *>(target), time,
                               easing, oldEasingOut);
        }
    }
    return false;
}

bool ApplySpatialTangentsVec2(AnimatableBase *target, FrameTime time,
                              const std::optional<Vec2> &spatialIn,
                              const std::optional<Vec2> &spatialOut,
                              std::optional<Vec2> *oldSpatialInOut,
                              std::optional<Vec2> *oldSpatialOutOut) {
    if (target == nullptr || target->valueType() != AnimatableType::Vec2) {
        return false;
    }
    auto *animatable = static_cast<Animatable<Vec2> *>(target);
    for (const Keyframe<Vec2> &keyframe : animatable->keyframes()) {
        if (keyframe.time != time) {
            continue;
        }
        if (oldSpatialInOut) {
            *oldSpatialInOut = keyframe.spatialInTangent;
        }
        if (oldSpatialOutOut) {
            *oldSpatialOutOut = keyframe.spatialOutTangent;
        }
        Keyframe<Vec2> updated = keyframe;
        updated.spatialInTangent = spatialIn;
        updated.spatialOutTangent = spatialOut;
        return animatable->updateKeyframe(time, std::move(updated));
    }
    return false;
}

int IndexOfLayer(const Composition &composition, EntityId layerId) {
    for (size_t i = 0; i < composition.layers.size(); ++i) {
        if (composition.layers[i]->id == layerId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace motion

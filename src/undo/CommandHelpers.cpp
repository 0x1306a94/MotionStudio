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
            return ApplyStaticValue(static_cast<Animatable<Vec3> *>(target), newValue,
                                    oldValue);
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
        case AnimatableType::Vec4: {
            return ApplyStaticValue(static_cast<Animatable<Vec4> *>(target), newValue,
                                    oldValue);
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
            return TakeKeyframeTyped(static_cast<Animatable<Vec3> *>(target), time);
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
        case AnimatableType::Vec4: {
            return TakeKeyframeTyped(static_cast<Animatable<Vec4> *>(target), time);
        }
    }
    return std::nullopt;
}

void AddKeyframeAny(AnimatableBase *target, const KeyframeData &keyframe) {
    // Silently skip when keyframe type does not match property type.
    if (const auto *typed = std::get_if<Keyframe<float>>(&keyframe)) {
        if (target->valueType() == AnimatableType::Float) {
            static_cast<Animatable<float> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<Vec2>>(&keyframe)) {
        if (target->valueType() == AnimatableType::Vec2) {
            static_cast<Animatable<Vec2> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<Vec3>>(&keyframe)) {
        if (target->valueType() == AnimatableType::Vec3) {
            static_cast<Animatable<Vec3> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<Color>>(&keyframe)) {
        if (target->valueType() == AnimatableType::Color) {
            static_cast<Animatable<Color> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<VectorNetwork>>(&keyframe)) {
        if (target->valueType() == AnimatableType::VectorNetwork) {
            static_cast<Animatable<VectorNetwork> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<std::string>>(&keyframe)) {
        if (target->valueType() == AnimatableType::String) {
            static_cast<Animatable<std::string> *>(target)->addKeyframe(*typed);
        }
        return;
    }
    if (const auto *typed = std::get_if<Keyframe<Vec4>>(&keyframe)) {
        if (target->valueType() == AnimatableType::Vec4) {
            static_cast<Animatable<Vec4> *>(target)->addKeyframe(*typed);
        }
    }
}

FrameTime KeyframeTime(const KeyframeData &keyframe) {
    return std::visit([](const auto &typed) {
        return typed.time;
    },
                      keyframe);
}

void SetKeyframeTime(KeyframeData &keyframe, FrameTime time) {
    std::visit([time](auto &typed) {
        typed.time = time;
    },
               keyframe);
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
            return ApplyEasing(static_cast<Animatable<Vec3> *>(target), time, easing,
                               oldEasingOut);
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
        case AnimatableType::Vec4: {
            return ApplyEasing(static_cast<Animatable<Vec4> *>(target), time, easing,
                               oldEasingOut);
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

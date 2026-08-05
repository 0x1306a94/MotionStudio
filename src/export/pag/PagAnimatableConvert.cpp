#include "PagAnimatableConvert.h"

#include <algorithm>
#include <cmath>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "base/keyframes/SingleEaseKeyframe.h"
#include "base/keyframes/SpatialPointKeyframe.h"

namespace motion {
namespace pag_export {
namespace {

pag::KeyframeInterpolationType MapInterpolation(const Easing &easing) {
    switch (easing.type) {
        case EasingType::Hold:
            return pag::KeyframeInterpolationType::Hold;
        case EasingType::Linear:
            return pag::KeyframeInterpolationType::Linear;
        case EasingType::Ease:
        case EasingType::EaseIn:
        case EasingType::EaseOut:
        case EasingType::EaseInOut:
        case EasingType::CubicBezier:
            return pag::KeyframeInterpolationType::Bezier;
    }
    return pag::KeyframeInterpolationType::Linear;
}

Easing ResolvedEasing(const Easing &easing) {
    switch (easing.type) {
        case EasingType::Ease:
            return Easing::Ease();
        case EasingType::EaseIn:
            return Easing::EaseIn();
        case EasingType::EaseOut:
            return Easing::EaseOut();
        case EasingType::EaseInOut:
            return Easing::EaseInOut();
        default:
            return easing;
    }
}

template <typename T>
void ApplyBezierHandles(pag::Keyframe<T> *keyframe, const Easing &easing, int dimensions) {
    const Easing resolved = ResolvedEasing(easing);
    if (MapInterpolation(resolved) != pag::KeyframeInterpolationType::Bezier) {
        return;
    }
    const pag::Point outHandle = pag::Point::Make(resolved.inX, resolved.inY);
    const pag::Point inHandle = pag::Point::Make(resolved.outX, resolved.outY);
    keyframe->bezierOut.assign(static_cast<size_t>(dimensions), outHandle);
    keyframe->bezierIn.assign(static_cast<size_t>(dimensions), inHandle);
}

float IdentityFloat(float value) {
    return value;
}

float Clamp01(float value) {
    return std::min(1.0f, std::max(0.0f, value));
}

uint8_t ChannelToU8(float channel) {
    return static_cast<uint8_t>(std::lround(Clamp01(channel) * 255.0f));
}

pag::Keyframe<float> *MakeFloatKeyframe(const Keyframe<float> &from, const Keyframe<float> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<float>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

pag::Keyframe<pag::Opacity> *MakeOpacityKeyframe(const Keyframe<float> &from,
                                                 const Keyframe<float> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::Opacity>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

pag::Keyframe<pag::Opacity> *MakeColorAlphaKeyframe(const Keyframe<Color> &from,
                                                    const Keyframe<Color> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::Opacity>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

pag::Keyframe<pag::Color> *MakeColorKeyframe(const Keyframe<Color> &from, const Keyframe<Color> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::Color>();
    ApplyBezierHandles(keyframe, from.easing, 3);
    return keyframe;
}

pag::Keyframe<pag::Point> *MakePointKeyframe(const Keyframe<Vec2> &from, const Keyframe<Vec2> &to) {
    const bool hasSpatial = from.spatialOutTangent.has_value() || to.spatialInTangent.has_value();
    pag::Keyframe<pag::Point> *keyframe = nullptr;
    if (hasSpatial) {
        keyframe = new pag::SpatialPointKeyframe();
        if (from.spatialOutTangent) {
            keyframe->spatialOut =
                pag::Point3D::Make(from.spatialOutTangent->x, from.spatialOutTangent->y, 0);
        }
        if (to.spatialInTangent) {
            keyframe->spatialIn =
                pag::Point3D::Make(to.spatialInTangent->x, to.spatialInTangent->y, 0);
        }
    } else {
        keyframe = new pag::SingleEaseKeyframe<pag::Point>();
    }
    ApplyBezierHandles(keyframe, from.easing, 2);
    return keyframe;
}

pag::Keyframe<pag::PathHandle> *MakePathKeyframe(const Keyframe<BezierPath> &from,
                                                 const Keyframe<BezierPath> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::PathHandle>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

pag::Keyframe<pag::PathHandle> *MakeNetworkPathKeyframe(const Keyframe<VectorNetwork> &from,
                                                        const Keyframe<VectorNetwork> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::PathHandle>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

BezierPath NetworkToExportPath(const VectorNetwork &network) {
    BezierPath fill = CompileFillFaces(network);
    if (!fill.contours.empty()) {
        return fill;
    }
    return CompileStrokeEdges(network);
}

pag::PathHandle ToPagPathFromNetwork(const VectorNetwork &network) {
    return ToPagPath(NetworkToExportPath(network));
}

pag::Keyframe<pag::Percent> *MakePercentKeyframe(const Keyframe<float> &from,
                                                 const Keyframe<float> &) {
    auto *keyframe = new pag::SingleEaseKeyframe<pag::Percent>();
    ApplyBezierHandles(keyframe, from.easing, 1);
    return keyframe;
}

template <typename TValue, typename TPag>
pag::Property<TPag> *ConvertAnimatable(
    const Animatable<TValue> &source, pag::Keyframe<TPag> *(*makeKeyframe)(const Keyframe<TValue> &, const Keyframe<TValue> &),
    TPag (*convertValue)(TValue)) {
    if (!source.isAnimated()) {
        return new pag::Property<TPag>(convertValue(source.staticValue()));
    }
    const auto &keyframes = source.keyframes();
    if (keyframes.size() == 1) {
        return new pag::Property<TPag>(convertValue(keyframes.front().value));
    }
    std::vector<pag::Keyframe<TPag> *> pagKeyframes;
    pagKeyframes.reserve(keyframes.size() - 1);
    for (size_t index = 0; index + 1 < keyframes.size(); ++index) {
        const Keyframe<TValue> &from = keyframes[index];
        const Keyframe<TValue> &to = keyframes[index + 1];
        if (to.time <= from.time) {
            continue;
        }
        pag::Keyframe<TPag> *pagKeyframe = makeKeyframe(from, to);
        pagKeyframe->startTime = from.time;
        pagKeyframe->endTime = to.time;
        pagKeyframe->startValue = convertValue(from.value);
        pagKeyframe->endValue = convertValue(to.value);
        pagKeyframe->interpolationType = MapInterpolation(from.easing);
        pagKeyframes.push_back(pagKeyframe);
    }
    if (pagKeyframes.empty()) {
        return new pag::Property<TPag>(convertValue(keyframes.front().value));
    }
    return new pag::AnimatableProperty<TPag>(pagKeyframes);
}

// Overload for convertValue taking const ref (Color, BezierPath).
template <typename TValue, typename TPag>
pag::Property<TPag> *ConvertAnimatableRef(
    const Animatable<TValue> &source, pag::Keyframe<TPag> *(*makeKeyframe)(const Keyframe<TValue> &, const Keyframe<TValue> &),
    TPag (*convertValue)(const TValue &)) {
    if (!source.isAnimated()) {
        return new pag::Property<TPag>(convertValue(source.staticValue()));
    }
    const auto &keyframes = source.keyframes();
    if (keyframes.size() == 1) {
        return new pag::Property<TPag>(convertValue(keyframes.front().value));
    }
    std::vector<pag::Keyframe<TPag> *> pagKeyframes;
    pagKeyframes.reserve(keyframes.size() - 1);
    for (size_t index = 0; index + 1 < keyframes.size(); ++index) {
        const Keyframe<TValue> &from = keyframes[index];
        const Keyframe<TValue> &to = keyframes[index + 1];
        if (to.time <= from.time) {
            continue;
        }
        pag::Keyframe<TPag> *pagKeyframe = makeKeyframe(from, to);
        pagKeyframe->startTime = from.time;
        pagKeyframe->endTime = to.time;
        pagKeyframe->startValue = convertValue(from.value);
        pagKeyframe->endValue = convertValue(to.value);
        pagKeyframe->interpolationType = MapInterpolation(from.easing);
        pagKeyframes.push_back(pagKeyframe);
    }
    if (pagKeyframes.empty()) {
        return new pag::Property<TPag>(convertValue(keyframes.front().value));
    }
    return new pag::AnimatableProperty<TPag>(pagKeyframes);
}

}  // namespace

pag::Point ToPagPoint(Vec2 value) {
    return pag::Point::Make(value.x, value.y);
}

pag::Color ToPagColor(const Color &value) {
    return pag::Color{ChannelToU8(value.r), ChannelToU8(value.g), ChannelToU8(value.b)};
}

pag::Opacity ToPagOpacity(float opacity) {
    return static_cast<pag::Opacity>(std::lround(Clamp01(opacity) * 255.0f));
}

pag::Opacity ToPagOpacityFromColor(const Color &value) {
    return ToPagOpacity(value.a);
}

pag::PathHandle ToPagPath(const BezierPath &path) {
    auto data = std::make_shared<pag::PathData>();
    for (const BezierPath::Contour &contour : path.contours) {
        if (contour.vertices.empty()) {
            continue;
        }
        const BezierPath::Vertex &first = contour.vertices.front();
        data->moveTo(first.point.x, first.point.y);
        for (size_t index = 1; index < contour.vertices.size(); ++index) {
            const BezierPath::Vertex &previous = contour.vertices[index - 1];
            const BezierPath::Vertex &current = contour.vertices[index];
            const float controlX1 = previous.point.x + previous.outTangent.x;
            const float controlY1 = previous.point.y + previous.outTangent.y;
            const float controlX2 = current.point.x + current.inTangent.x;
            const float controlY2 = current.point.y + current.inTangent.y;
            const bool isLine = previous.outTangent.x == 0 && previous.outTangent.y == 0 &&
                current.inTangent.x == 0 && current.inTangent.y == 0;
            if (isLine) {
                data->lineTo(current.point.x, current.point.y);
            } else {
                data->cubicTo(controlX1, controlY1, controlX2, controlY2, current.point.x,
                              current.point.y);
            }
        }
        if (contour.closed) {
            const BezierPath::Vertex &last = contour.vertices.back();
            const float controlX1 = last.point.x + last.outTangent.x;
            const float controlY1 = last.point.y + last.outTangent.y;
            const float controlX2 = first.point.x + first.inTangent.x;
            const float controlY2 = first.point.y + first.inTangent.y;
            const bool isLine = last.outTangent.x == 0 && last.outTangent.y == 0 &&
                first.inTangent.x == 0 && first.inTangent.y == 0;
            if (!isLine) {
                data->cubicTo(controlX1, controlY1, controlX2, controlY2, first.point.x,
                              first.point.y);
            }
            data->close();
        }
    }
    return data;
}

pag::Property<float> *ConvertFloat(const Animatable<float> &source,
                                   std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatable(source, MakeFloatKeyframe, IdentityFloat);
}

pag::Property<pag::Point> *ConvertPoint(const Animatable<Vec2> &source,
                                        std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatable(source, MakePointKeyframe, ToPagPoint);
}

pag::Property<pag::Color> *ConvertColor(const Animatable<Color> &source,
                                        std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatableRef(source, MakeColorKeyframe, ToPagColor);
}

pag::Property<pag::Opacity> *ConvertColorAlpha(const Animatable<Color> &source,
                                               std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatableRef(source, MakeColorAlphaKeyframe, ToPagOpacityFromColor);
}

pag::Property<pag::Opacity> *ConvertOpacity(const Animatable<float> &source,
                                            std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatable(source, MakeOpacityKeyframe, ToPagOpacity);
}

pag::Property<pag::PathHandle> *ConvertPath(const Animatable<VectorNetwork> &source,
                                            std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatableRef(source, MakeNetworkPathKeyframe, ToPagPathFromNetwork);
}

pag::Property<pag::PathHandle> *ConvertBezierPath(const Animatable<BezierPath> &source,
                                                  std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatableRef(source, MakePathKeyframe, ToPagPath);
}

pag::Property<pag::Percent> *ConvertPercent(const Animatable<float> &source,
                                            std::vector<PagExportWarning> *, EntityId) {
    return ConvertAnimatable(source, MakePercentKeyframe, Clamp01);
}

}  // namespace pag_export
}  // namespace motion

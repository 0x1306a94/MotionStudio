#include "PagStrokeOutline.h"

#include <cmath>
#include <memory>
#include <vector>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/model/StrokeDash.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "base/keyframes/SingleEaseKeyframe.h"

#include "tgfx/core/Path.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Stroke.h"

namespace motion {
namespace pag_export {
namespace {

struct TrimWindow {
    float start = 0.0f;
    float end = 1.0f;
};

template <typename T>
void CollectAnimatableTimes(const Animatable<T> &value, std::set<FrameTime> *times) {
    if (!value.isAnimated()) {
        return;
    }
    for (const Keyframe<T> &keyframe : value.keyframes()) {
        times->insert(keyframe.time);
    }
}

void CollectGeometryBakeTimes(const ShapeElement &element, std::set<FrameTime> *times) {
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &path = static_cast<const ShapePath &>(element);
            CollectAnimatableTimes(path.path, times);
            break;
        }
        case ShapeType::Rect: {
            const auto &rect = static_cast<const ShapeRect &>(element);
            CollectAnimatableTimes(rect.position, times);
            CollectAnimatableTimes(rect.size, times);
            CollectAnimatableTimes(rect.cornerRadius, times);
            break;
        }
        case ShapeType::Ellipse: {
            const auto &ellipse = static_cast<const ShapeEllipse &>(element);
            CollectAnimatableTimes(ellipse.position, times);
            CollectAnimatableTimes(ellipse.size, times);
            break;
        }
        case ShapeType::TrimPath:
            break;
    }
}

ShapeGeometry GeometryAt(const ShapeElement &element, FrameTime time) {
    const PreviewTime preview = static_cast<PreviewTime>(time);
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &path = static_cast<const ShapePath &>(element);
            const VectorNetwork network = path.path.evaluatePreview(preview);
            ShapeGeometry geometry = MakePathGeometry(CompileFillFaces(network));
            geometry.strokePath = CompileStrokeEdges(network);
            return geometry;
        }
        case ShapeType::Rect: {
            const auto &rect = static_cast<const ShapeRect &>(element);
            return MakeRectGeometry(rect.position.evaluatePreview(preview),
                                    rect.size.evaluatePreview(preview),
                                    rect.cornerRadius.evaluatePreview(preview));
        }
        case ShapeType::Ellipse: {
            const auto &ellipse = static_cast<const ShapeEllipse &>(element);
            return MakeEllipseGeometry(ellipse.position.evaluatePreview(preview),
                                       ellipse.size.evaluatePreview(preview));
        }
        case ShapeType::TrimPath:
            break;
    }
    return {};
}

bool IsStraightSegment(const BezierPath::Vertex &from, const BezierPath::Vertex &to) {
    return from.outTangent.x == 0.0f && from.outTangent.y == 0.0f && to.inTangent.x == 0.0f &&
        to.inTangent.y == 0.0f;
}

tgfx::Path BezierToTgfxPath(const BezierPath &path) {
    tgfx::Path result;
    for (const BezierPath::Contour &contour : path.contours) {
        if (contour.vertices.empty()) {
            continue;
        }
        const BezierPath::Vertex &first = contour.vertices.front();
        result.moveTo(first.point.x, first.point.y);
        for (size_t index = 1; index < contour.vertices.size(); ++index) {
            const BezierPath::Vertex &from = contour.vertices[index - 1];
            const BezierPath::Vertex &to = contour.vertices[index];
            if (IsStraightSegment(from, to)) {
                result.lineTo(to.point.x, to.point.y);
            } else {
                result.cubicTo(from.point.x + from.outTangent.x, from.point.y + from.outTangent.y,
                               to.point.x + to.inTangent.x, to.point.y + to.inTangent.y, to.point.x,
                               to.point.y);
            }
        }
        if (contour.closed && contour.vertices.size() > 1) {
            const BezierPath::Vertex &last = contour.vertices.back();
            if (IsStraightSegment(last, first)) {
                result.lineTo(first.point.x, first.point.y);
            } else {
                result.cubicTo(last.point.x + last.outTangent.x, last.point.y + last.outTangent.y,
                               first.point.x + first.inTangent.x, first.point.y + first.inTangent.y,
                               first.point.x, first.point.y);
            }
            result.close();
        }
    }
    return result;
}

tgfx::LineCap ToTgfxCap(LineCap cap) {
    switch (cap) {
        case LineCap::Round:
            return tgfx::LineCap::Round;
        case LineCap::Square:
            return tgfx::LineCap::Square;
        case LineCap::Butt:
            break;
    }
    return tgfx::LineCap::Butt;
}

tgfx::LineJoin ToTgfxJoin(LineJoin join) {
    switch (join) {
        case LineJoin::Round:
            return tgfx::LineJoin::Round;
        case LineJoin::Bevel:
            return tgfx::LineJoin::Bevel;
        case LineJoin::Miter:
            break;
    }
    return tgfx::LineJoin::Miter;
}

// Same normalization as adapter/tgfx NormalizeTrimWindow.
TrimWindow NormalizeTrimWindow(float trimStart, float trimEnd, float trimOffset) {
    const float shift = trimOffset / 360.0f;
    float start = trimStart + shift;
    float end = trimEnd + shift;
    const float base = std::floor(start);
    start -= base;
    end -= base;
    if (end < start) {
        end += 1.0f;
    }
    return TrimWindow{start, end};
}

tgfx::Path ApplyTrimWindow(const tgfx::Path &path, const TrimWindow &window) {
    tgfx::Path result = path;
    if (window.start == window.end) {
        result.reset();
        return result;
    }
    auto effect = tgfx::PathEffect::MakeTrim(window.start, window.end);
    if (effect != nullptr) {
        effect->filterPath(&result);
    }
    return result;
}

// strokeGeometry may be trimmed; fullPath is the untrimmed shape for Inside/Outside boolean.
tgfx::Path BuildPositionedOutline(const tgfx::Path &strokeGeometry, const tgfx::Path &fullPath,
                                  float width, LineCap cap, LineJoin join, float miterLimit,
                                  StrokePosition position) {
    if (width <= 0.0f || strokeGeometry.isEmpty() || fullPath.isEmpty()) {
        return {};
    }
    tgfx::Stroke stroke(width * 2.0f, ToTgfxCap(cap), ToTgfxJoin(join), miterLimit);
    tgfx::Path outline = strokeGeometry;
    if (!stroke.applyToPath(&outline)) {
        return {};
    }
    outline.addPath(fullPath, position == StrokePosition::Inside ? tgfx::PathOp::Intersect : tgfx::PathOp::Difference);
    return outline;
}

void AppendQuadAsCubic(pag::PathData *data, const tgfx::Point &p0, const tgfx::Point &p1,
                       const tgfx::Point &p2) {
    const float c1x = p0.x + (2.0f / 3.0f) * (p1.x - p0.x);
    const float c1y = p0.y + (2.0f / 3.0f) * (p1.y - p0.y);
    const float c2x = p2.x + (2.0f / 3.0f) * (p1.x - p2.x);
    const float c2y = p2.y + (2.0f / 3.0f) * (p1.y - p2.y);
    data->cubicTo(c1x, c1y, c2x, c2y, p2.x, p2.y);
}

pag::PathHandle TgfxPathToPagPath(const tgfx::Path &path) {
    auto data = std::make_shared<pag::PathData>();
    for (const tgfx::Path::Segment &segment : path) {
        switch (segment.verb) {
            case tgfx::PathVerb::Move:
                data->moveTo(segment.points[0].x, segment.points[0].y);
                break;
            case tgfx::PathVerb::Line:
                data->lineTo(segment.points[1].x, segment.points[1].y);
                break;
            case tgfx::PathVerb::Quad:
                AppendQuadAsCubic(data.get(), segment.points[0], segment.points[1],
                                  segment.points[2]);
                break;
            case tgfx::PathVerb::Conic: {
                const std::vector<tgfx::Point> quads = tgfx::Path::ConvertConicToQuads(
                    segment.points[0], segment.points[1], segment.points[2], segment.conicWeight,
                    1);
                for (size_t index = 1; index + 1 < quads.size(); index += 2) {
                    AppendQuadAsCubic(data.get(), quads[index - 1], quads[index], quads[index + 1]);
                }
                break;
            }
            case tgfx::PathVerb::Cubic:
                data->cubicTo(segment.points[1].x, segment.points[1].y, segment.points[2].x,
                              segment.points[2].y, segment.points[3].x, segment.points[3].y);
                break;
            case tgfx::PathVerb::Close:
                data->close();
                break;
            case tgfx::PathVerb::Done:
                break;
        }
    }
    return data;
}

}  // namespace

void CollectStrokeOutlineBakeTimes(const ShapeElement &geometry, const StrokeStyle &stroke,
                                   FrameTime fallbackTime, std::set<FrameTime> *times) {
    if (times == nullptr) {
        return;
    }
    CollectGeometryBakeTimes(geometry, times);
    CollectAnimatableTimes(stroke.width, times);
    CollectAnimatableTimes(stroke.trimStart, times);
    CollectAnimatableTimes(stroke.trimEnd, times);
    CollectAnimatableTimes(stroke.trimOffset, times);
    CollectAnimatableTimes(stroke.dashOffset, times);
    if (times->empty()) {
        times->insert(fallbackTime);
    }
}

pag::PathHandle BakePositionedStrokeOutline(const ShapeElement &geometry, const StrokeStyle &stroke,
                                            FrameTime time) {
    if (stroke.position == StrokePosition::Center) {
        return {};
    }
    const ShapeGeometry shape = GeometryAt(geometry, time);
    if (shape.isZero()) {
        return {};
    }
    const BezierPath fillPath = ShapeGeometryToBezierPath(shape);
    const BezierPath strokePath = ShapeGeometryStrokePath(shape);
    const tgfx::Path fullPath = BezierToTgfxPath(fillPath);
    const tgfx::Path strokeSource = BezierToTgfxPath(strokePath);
    // Match MS: trim the stroke geometry, boolean against the full path for Inside/Outside.
    const TrimWindow window =
        NormalizeTrimWindow(stroke.trimStart.evaluate(time), stroke.trimEnd.evaluate(time),
                            stroke.trimOffset.evaluate(time));
    tgfx::Path strokeGeometry =
        (window.end - window.start < 1.0f - 1e-6f) ? ApplyTrimWindow(strokeSource, window)
                                                   : strokeSource;
    if (NeedsDash(stroke.strokeMode, stroke.dashes)) {
        const std::vector<float> intervals = NormalizeDashArray(stroke.dashes);
        auto effect = tgfx::PathEffect::MakeDash(intervals.data(), static_cast<int>(intervals.size()),
                                                 stroke.dashOffset.evaluate(time), false);
        if (effect != nullptr) {
            effect->filterPath(&strokeGeometry);
        }
    }
    const float width = stroke.width.evaluate(time);
    const tgfx::Path outline =
        BuildPositionedOutline(strokeGeometry, fullPath, width, stroke.cap, stroke.join,
                               stroke.miterLimit, stroke.position);
    if (outline.isEmpty()) {
        return {};
    }
    return TgfxPathToPagPath(outline);
}

pag::Property<pag::PathHandle> *MakeStrokeOutlinePathProperty(
    const ShapeElement &geometry, const StrokeStyle &stroke, const std::vector<FrameTime> &times) {
    if (times.empty()) {
        return nullptr;
    }
    if (times.size() == 1) {
        pag::PathHandle path = BakePositionedStrokeOutline(geometry, stroke, times.front());
        if (path == nullptr) {
            return nullptr;
        }
        return new pag::Property<pag::PathHandle>(path);
    }

    std::vector<pag::PathHandle> samples;
    samples.reserve(times.size());
    for (FrameTime time : times) {
        pag::PathHandle path = BakePositionedStrokeOutline(geometry, stroke, time);
        if (path == nullptr) {
            return nullptr;
        }
        samples.push_back(std::move(path));
    }

    std::vector<pag::Keyframe<pag::PathHandle> *> pagKeyframes;
    pagKeyframes.reserve(times.size() - 1);
    for (size_t index = 0; index + 1 < times.size(); ++index) {
        if (times[index + 1] <= times[index]) {
            continue;
        }
        auto *keyframe = new pag::SingleEaseKeyframe<pag::PathHandle>();
        keyframe->startTime = times[index];
        keyframe->endTime = times[index + 1];
        keyframe->startValue = samples[index];
        keyframe->endValue = samples[index + 1];
        keyframe->interpolationType = pag::KeyframeInterpolationType::Hold;
        pagKeyframes.push_back(keyframe);
    }
    if (pagKeyframes.empty()) {
        return new pag::Property<pag::PathHandle>(samples.front());
    }
    return new pag::AnimatableProperty<pag::PathHandle>(pagKeyframes);
}

}  // namespace pag_export
}  // namespace motion

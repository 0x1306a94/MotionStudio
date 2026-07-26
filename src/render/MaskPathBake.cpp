#include "MotionStudio/render/MaskPathBake.h"

#include <algorithm>
#include <vector>

#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

BezierPath FallbackMaskPath() {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{-100, -100}, {}, {}});
    path.vertices.push_back({{100, -100}, {}, {}});
    path.vertices.push_back({{100, 100}, {}, {}});
    path.vertices.push_back({{-100, 100}, {}, {}});
    return path;
}

void AppendShapeGeometry(const ShapeElement &element, PreviewTime time,
                         std::vector<ShapeGeometry> &geometries) {
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            geometries.push_back(MakePathGeometry(shape.path.evaluatePreview(time)));
            break;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            const Vec2 center = shape.position.evaluatePreview(time);
            const Vec2 size = shape.size.evaluatePreview(time);
            const float halfWidth = std::max(size.x * 0.5f, 0.0f);
            const float halfHeight = std::max(size.y * 0.5f, 0.0f);
            const float cornerRadius = std::clamp(shape.cornerRadius.evaluatePreview(time), 0.0f,
                                                  std::min(halfWidth, halfHeight));
            geometries.push_back(MakeRectGeometry(center, size, cornerRadius));
            break;
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            geometries.push_back(MakeEllipseGeometry(shape.position.evaluatePreview(time),
                                                     shape.size.evaluatePreview(time)));
            break;
        }
        case ShapeType::TrimPath: {
            break;
        }
    }
}

}  // namespace

BezierPath BakeMaskPathFromLayer(const Layer &layer, FrameTime time) {
    if (layer.content == nullptr || layer.content->type() != LayerType::Shape) {
        return FallbackMaskPath();
    }
    const auto *shapeContent = static_cast<const ShapeContent *>(layer.content.get());
    if (shapeContent->geometry == nullptr) {
        return FallbackMaskPath();
    }

    std::vector<ShapeGeometry> geometries;
    AppendShapeGeometry(*shapeContent->geometry, static_cast<PreviewTime>(time), geometries);
    if (geometries.empty()) {
        return FallbackMaskPath();
    }

    BezierPath path = ShapeGeometryToBezierPath(geometries.front());
    if (path.vertices.empty()) {
        return FallbackMaskPath();
    }
    return path;
}

}  // namespace motion

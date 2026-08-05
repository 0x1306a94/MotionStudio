#include "MotionStudio/render/ShapeGeometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace motion {

namespace {

constexpr float kEllipseKappa = 0.5522847498f;

BezierPath RectToBezierPath(Vec2 center, Vec2 size, float cornerRadius) {
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float radius =
        std::clamp(cornerRadius, 0.0f, std::min(halfWidth, halfHeight));
    const float left = center.x - halfWidth;
    const float right = center.x + halfWidth;
    const float top = center.y - halfHeight;
    const float bottom = center.y + halfHeight;

    if (radius <= 0) {
        return MakeSingleContour({{{left, top}, {}, {}},
                                  {{right, top}, {}, {}},
                                  {{right, bottom}, {}, {}},
                                  {{left, bottom}, {}, {}}},
                                 true);
    }
    const float k = kEllipseKappa * radius;
    return MakeSingleContour({{{left, top + radius}, {}, {0, -k}},
                              {{left + radius, top}, {-k, 0}, {}},
                              {{right - radius, top}, {}, {k, 0}},
                              {{right, top + radius}, {0, -k}, {}},
                              {{right, bottom - radius}, {}, {0, k}},
                              {{right - radius, bottom}, {k, 0}, {}},
                              {{left + radius, bottom}, {}, {-k, 0}},
                              {{left, bottom - radius}, {0, k}, {}}},
                             true);
}

BezierPath EllipseToBezierPath(Vec2 center, Vec2 size) {
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float kx = kEllipseKappa * halfWidth;
    const float ky = kEllipseKappa * halfHeight;

    return MakeSingleContour(
        {{{center.x + halfWidth, center.y}, {0, -ky}, {0, ky}},
         {{center.x, center.y + halfHeight}, {kx, 0}, {-kx, 0}},
         {{center.x - halfWidth, center.y}, {0, ky}, {0, -ky}},
         {{center.x, center.y - halfHeight}, {-kx, 0}, {kx, 0}}},
        true);
}

bool PathHasClosedContour(const BezierPath &path) {
    for (const BezierPath::Contour &contour : path.contours) {
        if (contour.closed && !contour.vertices.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

ShapeGeometry MakePathGeometry(BezierPath path) {
    ShapeGeometry geometry;
    geometry.kind = ShapeGeometryKind::Path;
    geometry.path = std::move(path);
    return geometry;
}

ShapeGeometry MakeRectGeometry(Vec2 center, Vec2 size, float cornerRadius) {
    ShapeGeometry geometry;
    geometry.kind = ShapeGeometryKind::Rect;
    geometry.center = center;
    geometry.size = size;
    geometry.cornerRadius = cornerRadius;
    return geometry;
}

ShapeGeometry MakeEllipseGeometry(Vec2 center, Vec2 size) {
    ShapeGeometry geometry;
    geometry.kind = ShapeGeometryKind::Ellipse;
    geometry.center = center;
    geometry.size = size;
    return geometry;
}

bool ShapeGeometry::isZero() const {
    switch (kind) {
        case ShapeGeometryKind::Path: {
            return path.isZero() && strokePath.isZero();
        }
        case ShapeGeometryKind::Rect:
        case ShapeGeometryKind::Ellipse: {
            return size.x <= 0.0f && size.y <= 0.0f;
        }
    }
    return true;
}

bool ShapeGeometryIsClosed(const ShapeGeometry &geometry) {
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            return PathHasClosedContour(geometry.path);
        }
        case ShapeGeometryKind::Rect:
        case ShapeGeometryKind::Ellipse: {
            return true;
        }
    }
    return false;
}

BezierPath ShapeGeometryToBezierPath(const ShapeGeometry &geometry) {
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            return geometry.path;
        }
        case ShapeGeometryKind::Rect: {
            return RectToBezierPath(geometry.center, geometry.size, geometry.cornerRadius);
        }
        case ShapeGeometryKind::Ellipse: {
            return EllipseToBezierPath(geometry.center, geometry.size);
        }
    }
    return {};
}

BezierPath ShapeGeometryStrokePath(const ShapeGeometry &geometry) {
    if (geometry.kind == ShapeGeometryKind::Path && !geometry.strokePath.contours.empty()) {
        return geometry.strokePath;
    }
    return ShapeGeometryToBezierPath(geometry);
}

}  // namespace motion

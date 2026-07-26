#include "MotionStudio/render/ShapeGeometry.h"

#include <algorithm>
#include <cmath>

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

    BezierPath path;
    path.closed = true;
    if (radius <= 0) {
        path.vertices.push_back({{left, top}, {}, {}});
        path.vertices.push_back({{right, top}, {}, {}});
        path.vertices.push_back({{right, bottom}, {}, {}});
        path.vertices.push_back({{left, bottom}, {}, {}});
        return path;
    }
    const float k = kEllipseKappa * radius;
    path.vertices.push_back({{left, top + radius}, {}, {0, -k}});
    path.vertices.push_back({{left + radius, top}, {-k, 0}, {}});
    path.vertices.push_back({{right - radius, top}, {}, {k, 0}});
    path.vertices.push_back({{right, top + radius}, {0, -k}, {}});
    path.vertices.push_back({{right, bottom - radius}, {}, {0, k}});
    path.vertices.push_back({{right - radius, bottom}, {k, 0}, {}});
    path.vertices.push_back({{left + radius, bottom}, {}, {-k, 0}});
    path.vertices.push_back({{left, bottom - radius}, {0, k}, {}});
    return path;
}

BezierPath EllipseToBezierPath(Vec2 center, Vec2 size) {
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float kx = kEllipseKappa * halfWidth;
    const float ky = kEllipseKappa * halfHeight;

    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{center.x + halfWidth, center.y}, {0, -ky}, {0, ky}});
    path.vertices.push_back({{center.x, center.y + halfHeight}, {kx, 0}, {-kx, 0}});
    path.vertices.push_back({{center.x - halfWidth, center.y}, {0, ky}, {0, -ky}});
    path.vertices.push_back({{center.x, center.y - halfHeight}, {-kx, 0}, {kx, 0}});
    return path;
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

bool ShapeGeometryIsClosed(const ShapeGeometry &geometry) {
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            return geometry.path.closed;
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

}  // namespace motion

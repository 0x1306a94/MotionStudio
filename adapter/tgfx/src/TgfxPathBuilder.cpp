#include "TgfxPathBuilder.h"

#include <algorithm>
#include <cstring>

#include <tgfx/core/Rect.h>

#include "TgfxTypeConvert.h"

namespace motion {

namespace {

// Zero relative tangents mean a straight segment; emit lineTo so tgfx can
// recognize axis-aligned rectangles via Path::isRect and take the drawRect
// fast path instead of generic cubic triangulation.
bool IsStraightSegment(const BezierPath::Vertex &from, const BezierPath::Vertex &to) {
    return from.outTangent.x == 0.0f && from.outTangent.y == 0.0f && to.inTangent.x == 0.0f &&
        to.inTangent.y == 0.0f;
}

void AppendBezierSegment(tgfx::Path &result, const BezierPath::Vertex &from,
                         const BezierPath::Vertex &to) {
    if (IsStraightSegment(from, to)) {
        result.lineTo(to.point.x, to.point.y);
        return;
    }
    result.cubicTo(from.point.x + from.outTangent.x, from.point.y + from.outTangent.y,
                   to.point.x + to.inTangent.x, to.point.y + to.inTangent.y, to.point.x,
                   to.point.y);
}

tgfx::Path BezierToTgfxPath(const BezierPath &path, FillRule fillRule) {
    tgfx::Path result;
    for (const BezierPath::Contour &contour : path.contours) {
        if (contour.vertices.empty()) {
            continue;
        }
        const BezierPath::Vertex &first = contour.vertices.front();
        result.moveTo(first.point.x, first.point.y);
        for (size_t i = 1; i < contour.vertices.size(); ++i) {
            AppendBezierSegment(result, contour.vertices[i - 1], contour.vertices[i]);
        }
        if (contour.closed && contour.vertices.size() > 1) {
            AppendBezierSegment(result, contour.vertices.back(), first);
            result.close();
        }
    }
    result.setFillType(ToTgfxFillType(fillRule));
    return result;
}

tgfx::Rect CenteredBounds(Vec2 center, Vec2 size) {
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    return tgfx::Rect::MakeXYWH(center.x - halfWidth, center.y - halfHeight, halfWidth * 2.0f,
                                halfHeight * 2.0f);
}

}  // namespace

uint64_t MixHash(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t FloatBits(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float size mismatch");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

tgfx::Path BuildTgfxPath(const ShapeGeometry &geometry, FillRule fillRule) {
    tgfx::Path result;
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            return BezierToTgfxPath(geometry.path, fillRule);
        }
        case ShapeGeometryKind::Rect: {
            const tgfx::Rect bounds = CenteredBounds(geometry.center, geometry.size);
            const float maxRadius = std::min(bounds.width(), bounds.height()) * 0.5f;
            const float radius = std::clamp(geometry.cornerRadius, 0.0f, maxRadius);
            if (radius > 0.0f) {
                result.addRoundRect(bounds, radius, radius);
            } else {
                result.addRect(bounds);
            }
            break;
        }
        case ShapeGeometryKind::Ellipse: {
            result.addOval(CenteredBounds(geometry.center, geometry.size));
            break;
        }
    }
    result.setFillType(ToTgfxFillType(fillRule));
    return result;
}

uint64_t HashGeometry(const ShapeGeometry &geometry, FillRule fillRule) {
    uint64_t hash = static_cast<uint64_t>(geometry.kind);
    hash = MixHash(hash, static_cast<uint64_t>(fillRule));
    switch (geometry.kind) {
        case ShapeGeometryKind::Path: {
            hash = MixHash(hash, geometry.path.contours.size());
            for (const BezierPath::Contour &contour : geometry.path.contours) {
                hash = MixHash(hash, contour.closed ? 1ULL : 0ULL);
                hash = MixHash(hash, contour.vertices.size());
                for (const BezierPath::Vertex &vertex : contour.vertices) {
                    hash = MixHash(hash, FloatBits(vertex.point.x));
                    hash = MixHash(hash, FloatBits(vertex.point.y));
                    hash = MixHash(hash, FloatBits(vertex.inTangent.x));
                    hash = MixHash(hash, FloatBits(vertex.inTangent.y));
                    hash = MixHash(hash, FloatBits(vertex.outTangent.x));
                    hash = MixHash(hash, FloatBits(vertex.outTangent.y));
                }
            }
            break;
        }
        case ShapeGeometryKind::Rect: {
            hash = MixHash(hash, FloatBits(geometry.center.x));
            hash = MixHash(hash, FloatBits(geometry.center.y));
            hash = MixHash(hash, FloatBits(geometry.size.x));
            hash = MixHash(hash, FloatBits(geometry.size.y));
            hash = MixHash(hash, FloatBits(geometry.cornerRadius));
            break;
        }
        case ShapeGeometryKind::Ellipse: {
            hash = MixHash(hash, FloatBits(geometry.center.x));
            hash = MixHash(hash, FloatBits(geometry.center.y));
            hash = MixHash(hash, FloatBits(geometry.size.x));
            hash = MixHash(hash, FloatBits(geometry.size.y));
            break;
        }
    }
    return hash;
}

}  // namespace motion

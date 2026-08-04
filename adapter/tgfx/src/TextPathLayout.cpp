#include "TextPathLayout.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include <tgfx/core/Path.h>
#include <tgfx/core/PathMeasure.h>

#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/render/ShapeGeometry.h"
#include "MotionStudio/textlayout/TextLayout.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxPathBuilder.h"
#include "TgfxTextTypeface.h"

namespace motion {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct LaidOutGlyph {
    std::string utf8;
    float x = 0.0f;
    float y = 0.0f;
    float advance = 0.0f;
};

BezierPath ReverseBezierPath(const BezierPath &path) {
    BezierPath reversed;
    reversed.closed = path.closed;
    reversed.vertices.reserve(path.vertices.size());
    for (auto it = path.vertices.rbegin(); it != path.vertices.rend(); ++it) {
        BezierPath::Vertex vertex;
        vertex.point = it->point;
        vertex.inTangent = it->outTangent;
        vertex.outTangent = it->inTangent;
        reversed.vertices.push_back(vertex);
    }
    return reversed;
}

bool NextUtf8(const std::string &text, size_t &offset, std::string &utf8, uint32_t &unichar) {
    if (offset >= text.size()) {
        return false;
    }
    const size_t start = offset;
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    size_t byteCount = 1;
    if (lead < 0x80) {
        unichar = lead;
        byteCount = 1;
    } else if ((lead >> 5) == 0x6 && offset + 1 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x1F) << 6) |
            (static_cast<unsigned char>(text[offset + 1]) & 0x3F);
        byteCount = 2;
    } else if ((lead >> 4) == 0xE && offset + 2 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x0F) << 12) |
            ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 6) |
            (static_cast<unsigned char>(text[offset + 2]) & 0x3F);
        byteCount = 3;
    } else if ((lead >> 3) == 0x1E && offset + 3 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x07) << 18) |
            ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 12) |
            ((static_cast<unsigned char>(text[offset + 2]) & 0x3F) << 6) |
            (static_cast<unsigned char>(text[offset + 3]) & 0x3F);
        byteCount = 4;
    } else {
        unichar = 0xFFFD;
        byteCount = 1;
    }
    offset += byteCount;
    utf8 = text.substr(start, byteCount);
    return true;
}

float MapToPathPosition(float position, TextAlign align, float layoutWidth, float pathLength,
                        float firstMargin, float lastMargin, bool forceAlignment) {
    if (forceAlignment || align == TextAlign::Left) {
        return position + firstMargin;
    }
    if (align == TextAlign::Right) {
        return position - layoutWidth + pathLength + lastMargin;
    }
    return position + (pathLength - layoutWidth) * 0.5f + firstMargin + lastMargin;
}

float ForceAlignmentSpacing(float pathLength, float firstMargin, float lastMargin,
                            float totalAdvance, size_t glyphCount) {
    if (glyphCount <= 1) {
        return 0.0f;
    }
    const float segmentLength = std::fabs(pathLength + lastMargin - firstMargin);
    const float count = static_cast<float>(glyphCount - 1);
    if (firstMargin <= pathLength + lastMargin) {
        return (segmentLength - totalAdvance) / count;
    }
    return -(totalAdvance + segmentLength) / count;
}

void AppendTgfxPath(tgfx::Path &destination, const tgfx::Path &source) {
    destination.addPath(source);
}

std::unique_ptr<tgfx::PathMeasure> CreateExtendedPathMeasure(const tgfx::Path &basePath, float first,
                                                             float end) {
    auto measure = tgfx::PathMeasure::MakeFrom(basePath);
    if (measure == nullptr) {
        return nullptr;
    }
    const float pathLength = measure->getLength();
    if (measure->isClosed() || (first >= 0.0f && end <= pathLength)) {
        return measure;
    }

    tgfx::Path extended = basePath;
    if (first < 0.0f) {
        tgfx::Point pos{};
        tgfx::Point tan{};
        if (measure->getPosTan(0.0f, &pos, &tan)) {
            const float remain = std::fabs(first);
            tgfx::Path prefix;
            prefix.moveTo(pos.x - remain * tan.x, pos.y - remain * tan.y);
            prefix.lineTo(pos.x, pos.y);
            AppendTgfxPath(prefix, basePath);
            extended = prefix;
        }
    }
    if (end > pathLength) {
        auto temp = tgfx::PathMeasure::MakeFrom(extended);
        if (temp != nullptr) {
            tgfx::Point pos{};
            tgfx::Point tan{};
            const float tempLength = temp->getLength();
            if (temp->getPosTan(tempLength, &pos, &tan)) {
                const float remain = end - pathLength;
                extended.lineTo(pos.x + remain * tan.x, pos.y + remain * tan.y);
            }
        }
    }
    return tgfx::PathMeasure::MakeFrom(extended);
}

void ExpandBounds(Vec2 &minPoint, Vec2 &maxPoint, Vec2 point, bool &initialized) {
    if (!initialized) {
        minPoint = point;
        maxPoint = point;
        initialized = true;
        return;
    }
    minPoint.x = std::min(minPoint.x, point.x);
    minPoint.y = std::min(minPoint.y, point.y);
    maxPoint.x = std::max(maxPoint.x, point.x);
    maxPoint.y = std::max(maxPoint.y, point.y);
}

}  // namespace

TextPathLayoutResult LayoutTextOnPath(const TextPathLayoutInput &input) {
    TextPathLayoutResult result;
    result.boundsMin = {};
    result.boundsMax = {};
    if (input.path.vertices.empty()) {
        return result;
    }

    std::shared_ptr<tgfx::Typeface> typeface =
        ResolveTextTypeface(input.fontFamily, input.fontStyle);
    if (typeface == nullptr) {
        return result;
    }

    TgfxGlyphMetrics glyphMetrics(typeface);
    textlayout::TextLayoutInput layoutInput;
    layoutInput.text = input.text;
    layoutInput.softWrap = false;
    layoutInput.shrinkToFit = false;
    layoutInput.boxWidth = 1.0f;
    layoutInput.boxHeight = 1.0f;
    layoutInput.fontSize = input.fontSize > 0.0f ? input.fontSize : 1.0f;
    switch (input.align) {
        case TextAlign::Left: {
            layoutInput.align = textlayout::Align::Left;
            break;
        }
        case TextAlign::Center: {
            layoutInput.align = textlayout::Align::Center;
            break;
        }
        case TextAlign::Right: {
            layoutInput.align = textlayout::Align::Right;
            break;
        }
    }
    layoutInput.metrics = &glyphMetrics;
    const textlayout::TextLayoutResult layout = textlayout::LayoutText(layoutInput);
    const textlayout::FontMetrics fontMetrics = glyphMetrics.metrics(layout.appliedFontSize);
    const float layoutWidth = layout.measuredSize.x;

    BezierPath pathCopy = input.reversed ? ReverseBezierPath(input.path) : input.path;
    const tgfx::Path tgfxPath = BuildTgfxPath(MakePathGeometry(pathCopy), FillRule::NonZero);
    auto baseMeasure = tgfx::PathMeasure::MakeFrom(tgfxPath);
    if (baseMeasure == nullptr) {
        return result;
    }
    const float pathLength = baseMeasure->getLength();

    std::vector<LaidOutGlyph> glyphs;
    for (const textlayout::TextLine &line : layout.lines) {
        const float normalOffset = line.y - fontMetrics.ascent;
        float cursorX = line.x;
        size_t offset = 0;
        std::string utf8;
        uint32_t unichar = 0;
        while (NextUtf8(line.text, offset, utf8, unichar)) {
            const float advance = glyphMetrics.advance(unichar, layout.appliedFontSize);
            LaidOutGlyph glyph;
            glyph.utf8 = std::move(utf8);
            glyph.x = cursorX;
            glyph.y = normalOffset;
            glyph.advance = advance;
            glyphs.push_back(std::move(glyph));
            cursorX += advance;
        }
    }

    if (input.forceAlignment && !glyphs.empty()) {
        float totalAdvance = 0.0f;
        for (const LaidOutGlyph &glyph : glyphs) {
            totalAdvance += glyph.advance;
        }
        const float spacing = ForceAlignmentSpacing(pathLength, input.firstMargin, input.lastMargin,
                                                    totalAdvance, glyphs.size());
        float posX = 0.0f;
        for (LaidOutGlyph &glyph : glyphs) {
            glyph.x = posX;
            posX += glyph.advance + spacing;
        }
    }

    float first = 0.0f;
    float end = pathLength;
    for (const LaidOutGlyph &glyph : glyphs) {
        first = std::min(first, MapToPathPosition(glyph.x, input.align, layoutWidth, pathLength, input.firstMargin, input.lastMargin, input.forceAlignment));
        end = std::max(end, MapToPathPosition(glyph.x + glyph.advance, input.align, layoutWidth, pathLength, input.firstMargin, input.lastMargin, input.forceAlignment));
    }

    auto pathMeasure = CreateExtendedPathMeasure(tgfxPath, first, end);
    if (pathMeasure == nullptr) {
        return result;
    }
    const bool isClosed = pathMeasure->isClosed();
    const float newPathLength = pathMeasure->getLength();
    float hOffset = 0.0f;
    if (!isClosed && first < 0.0f) {
        hOffset = std::fabs(first);
    }

    bool boundsInitialized = false;
    for (const LaidOutGlyph &glyph : glyphs) {
        const float halfWidth = glyph.advance * 0.5f;
        float x = MapToPathPosition(glyph.x + halfWidth, input.align, layoutWidth, pathLength,
                                    input.firstMargin, input.lastMargin, input.forceAlignment) +
            hOffset;
        if (x < 0.0f || x > newPathLength) {
            if (newPathLength > 0.0f) {
                x = std::fmod(x + newPathLength, newPathLength);
                if (x < 0.0f) {
                    x += newPathLength;
                }
            } else {
                x = 0.0f;
            }
        }

        tgfx::Point pos{};
        tgfx::Point tan{};
        if (!pathMeasure->getPosTan(x, &pos, &tan)) {
            pos.set(x, glyph.y);
            tan.set(1.0f, 0.0f);
        }

        // T(pos) * R * T(-halfWidth, y): halfWidth is local so baseline center stays on the
        // path after rotation (parent-space halfWidth subtraction drifts along the normal).
        Mat3 matrix = Mat3::Identity();
        if (input.perpendicular) {
            const float degrees = std::atan2(tan.y, tan.x) * (180.0f / kPi);
            matrix = Mat3::Translate({pos.x, pos.y}) * Mat3::Rotate(degrees) *
                Mat3::Translate({-halfWidth, glyph.y});
        } else {
            matrix = Mat3::Translate({pos.x - halfWidth, pos.y + glyph.y});
        }

        TextPathGlyph outGlyph;
        outGlyph.utf8 = glyph.utf8;
        outGlyph.matrix = matrix;
        outGlyph.advance = glyph.advance;
        result.glyphs.push_back(std::move(outGlyph));

        const Vec2 corners[4] = {
            matrix.transformPoint({0.0f, -fontMetrics.ascent}),
            matrix.transformPoint({glyph.advance, -fontMetrics.ascent}),
            matrix.transformPoint({0.0f, fontMetrics.descent}),
            matrix.transformPoint({glyph.advance, fontMetrics.descent}),
        };
        for (const Vec2 &corner : corners) {
            ExpandBounds(result.boundsMin, result.boundsMax, corner, boundsInitialized);
        }
    }

    if (!boundsInitialized) {
        result.boundsMin = {};
        result.boundsMax = {};
    }
    return result;
}

}  // namespace motion

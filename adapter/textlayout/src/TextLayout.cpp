#include "MotionStudio/textlayout/TextLayout.h"

#include <algorithm>
#include <cstdint>

namespace motion::textlayout {
namespace {

bool IsWhitespace(uint32_t unichar) {
    return unichar == ' ' || unichar == '\t';
}

bool NextUtf8(const std::string &text, size_t &offset, uint32_t &unichar, size_t &byteCount) {
    if (offset >= text.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80) {
        unichar = lead;
        byteCount = 1;
        offset += 1;
        return true;
    }
    if ((lead >> 5) == 0x6 && offset + 1 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x1F) << 6) |
            (static_cast<unsigned char>(text[offset + 1]) & 0x3F);
        byteCount = 2;
        offset += 2;
        return true;
    }
    if ((lead >> 4) == 0xE && offset + 2 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x0F) << 12) |
            ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 6) |
            (static_cast<unsigned char>(text[offset + 2]) & 0x3F);
        byteCount = 3;
        offset += 3;
        return true;
    }
    if ((lead >> 3) == 0x1E && offset + 3 < text.size()) {
        unichar = (static_cast<uint32_t>(lead & 0x07) << 18) |
            ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 12) |
            ((static_cast<unsigned char>(text[offset + 2]) & 0x3F) << 6) |
            (static_cast<unsigned char>(text[offset + 3]) & 0x3F);
        byteCount = 4;
        offset += 4;
        return true;
    }
    unichar = 0xFFFD;
    byteCount = 1;
    offset += 1;
    return true;
}

float LineHeightOf(const FontMetrics &metrics) {
    return metrics.ascent + metrics.descent + metrics.leading;
}

float AlignX(Align align, float boxWidth, float lineWidth) {
    switch (align) {
        case Align::Left: {
            return 0.0f;
        }
        case Align::Center: {
            return (boxWidth - lineWidth) * 0.5f;
        }
        case Align::Right: {
            return boxWidth - lineWidth;
        }
    }
    return 0.0f;
}

float MeasureWidth(const std::string &text, const GlyphMetrics &metrics, float fontSize) {
    float width = 0.0f;
    size_t offset = 0;
    uint32_t unichar = 0;
    size_t byteCount = 0;
    while (NextUtf8(text, offset, unichar, byteCount)) {
        width += metrics.advance(unichar, fontSize);
    }
    return width;
}

void PushLine(TextLayoutResult &result, std::string lineText, float lineWidth, Align align,
              float boxWidth, const FontMetrics &metrics) {
    TextLine line;
    line.width = lineWidth;
    line.x = AlignX(align, boxWidth, lineWidth);
    line.y = metrics.ascent + static_cast<float>(result.lines.size()) * LineHeightOf(metrics);
    line.text = std::move(lineText);
    result.lines.push_back(std::move(line));
}

std::string TrimLeadingWhitespace(const std::string &text) {
    size_t offset = 0;
    while (offset < text.size()) {
        size_t next = offset;
        uint32_t unichar = 0;
        size_t byteCount = 0;
        if (!NextUtf8(text, next, unichar, byteCount)) {
            break;
        }
        if (!IsWhitespace(unichar)) {
            break;
        }
        offset = next;
    }
    return text.substr(offset);
}

TextLayoutResult LayoutAtFontSize(const TextLayoutInput &input, float fontSize) {
    TextLayoutResult result;
    result.appliedFontSize = fontSize;
    if (input.metrics == nullptr) {
        result.measuredSize = Vec2{0.0f, 0.0f};
        return result;
    }
    // Soft-wrap layout needs a positive box width; point text ignores boxWidth.
    if (input.softWrap && input.boxWidth <= 0.0f) {
        result.measuredSize = Vec2{0.0f, 0.0f};
        return result;
    }

    const GlyphMetrics &glyphMetrics = *input.metrics;
    const FontMetrics metrics = glyphMetrics.metrics(fontSize);
    const float lineHeight = LineHeightOf(metrics);
    // Provisional align width; point text realigns after measuring max line width.
    const float alignWidth = input.softWrap ? input.boxWidth : 0.0f;

    std::string current;
    float currentWidth = 0.0f;
    // Index into `current` of the first byte of the last whitespace run; npos = none.
    size_t lastWhitespaceStart = std::string::npos;
    float widthBeforeLastWhitespace = 0.0f;

    size_t offset = 0;
    while (offset < input.text.size()) {
        if (input.text[offset] == '\n') {
            PushLine(result, current, currentWidth, input.align, alignWidth, metrics);
            current.clear();
            currentWidth = 0.0f;
            lastWhitespaceStart = std::string::npos;
            widthBeforeLastWhitespace = 0.0f;
            offset += 1;
            continue;
        }

        const size_t glyphStart = offset;
        uint32_t unichar = 0;
        size_t byteCount = 0;
        if (!NextUtf8(input.text, offset, unichar, byteCount)) {
            break;
        }
        const float advance = glyphMetrics.advance(unichar, fontSize);

        if (input.softWrap && !current.empty() && currentWidth + advance > input.boxWidth) {
            if (lastWhitespaceStart != std::string::npos) {
                std::string head = current.substr(0, lastWhitespaceStart);
                PushLine(result, head, widthBeforeLastWhitespace, input.align, alignWidth, metrics);
                current = TrimLeadingWhitespace(current.substr(lastWhitespaceStart));
                currentWidth = MeasureWidth(current, glyphMetrics, fontSize);
            } else {
                PushLine(result, current, currentWidth, input.align, alignWidth, metrics);
                current.clear();
                currentWidth = 0.0f;
            }
            lastWhitespaceStart = std::string::npos;
            widthBeforeLastWhitespace = 0.0f;
        }

        if (IsWhitespace(unichar)) {
            if (lastWhitespaceStart == std::string::npos ||
                lastWhitespaceStart + 1 < current.size()) {
                // New whitespace run starts at end of current.
                lastWhitespaceStart = current.size();
                widthBeforeLastWhitespace = currentWidth;
            }
        } else {
            // Non-whitespace after whitespace ends the break candidate run tracking for "last".
            // Keep lastWhitespaceStart pointing at the most recent whitespace sequence start.
        }

        current.append(input.text, glyphStart, byteCount);
        currentWidth += advance;
    }

    PushLine(result, current, currentWidth, input.align, alignWidth, metrics);

    const float contentHeight =
        result.lines.empty() ? lineHeight : static_cast<float>(result.lines.size()) * lineHeight;
    if (!input.softWrap) {
        float maxLineWidth = 0.0f;
        for (const TextLine &line : result.lines) {
            maxLineWidth = std::max(maxLineWidth, line.width);
        }
        for (TextLine &line : result.lines) {
            line.x = AlignX(input.align, maxLineWidth, line.width);
        }
        result.measuredSize = Vec2{maxLineWidth, contentHeight};
    } else {
        result.measuredSize = Vec2{input.boxWidth, contentHeight};
    }
    return result;
}

bool FitsFixedBox(const TextLayoutResult &candidate, float boxWidth, float boxHeight) {
    if (candidate.measuredSize.y > boxHeight + 0.01f) {
        return false;
    }
    for (const TextLine &line : candidate.lines) {
        if (line.width > boxWidth + 0.01f) {
            return false;
        }
    }
    return true;
}

}  // namespace

TextLayoutResult LayoutText(const TextLayoutInput &input) {
    if (input.metrics == nullptr) {
        TextLayoutResult empty;
        empty.appliedFontSize = input.fontSize;
        empty.measuredSize = Vec2{0.0f, 0.0f};
        return empty;
    }

    // Point text never shrinks; shrink only applies to soft-wrapped box text.
    if (!input.softWrap || !input.shrinkToFit) {
        return LayoutAtFontSize(input, input.fontSize);
    }

    const float targetHeight = input.boxHeight;
    float low = 1.0f;
    float high = std::max(1.0f, input.fontSize);
    TextLayoutResult best = LayoutAtFontSize(input, low);

    for (int iter = 0; iter < 16; ++iter) {
        const float mid = (low + high) * 0.5f;
        TextLayoutResult candidate = LayoutAtFontSize(input, mid);
        if (FitsFixedBox(candidate, input.boxWidth, targetHeight)) {
            best = std::move(candidate);
            low = mid;
        } else {
            high = mid;
        }
    }

    best.measuredSize = Vec2{input.boxWidth, targetHeight};
    return best;
}

}  // namespace motion::textlayout

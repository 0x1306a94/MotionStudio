#include "SvgText.h"

#include <cmath>

#include "MotionStudio/model/TextContent.h"
#include "SvgTransform.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/core/Typeface.h"

namespace motion {
namespace svg {

namespace {

struct TextPosition {
    float x = 0.f;
    float y = 0.f;
    float dx = 0.f;
    float dy = 0.f;
};

struct TextRun {
    std::string text;
    ComputedStyle style;
    TextPosition position;
    const tgfx::SVGTextContainer *transformNode = nullptr;
};

bool IsBlank(const std::string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
            return false;
        }
    }
    return true;
}

bool ColorsClose(const Color &lhs, const Color &rhs) {
    return std::fabs(lhs.r - rhs.r) < 1e-4f && std::fabs(lhs.g - rhs.g) < 1e-4f &&
        std::fabs(lhs.b - rhs.b) < 1e-4f && std::fabs(lhs.a - rhs.a) < 1e-4f;
}

bool TextStylesDiffer(const ComputedStyle &lhs, const ComputedStyle &rhs) {
    if (lhs.hasFill != rhs.hasFill || lhs.fillIri != rhs.fillIri ||
        !ColorsClose(lhs.fillColor, rhs.fillColor) || lhs.fillOpacity != rhs.fillOpacity) {
        return true;
    }
    if (lhs.hasStroke != rhs.hasStroke || lhs.strokeIri != rhs.strokeIri ||
        !ColorsClose(lhs.strokeColor, rhs.strokeColor) || lhs.strokeWidth != rhs.strokeWidth) {
        return true;
    }
    if (lhs.fontFamily != rhs.fontFamily || lhs.fontSize != rhs.fontSize ||
        lhs.fontStyle != rhs.fontStyle || lhs.fontBold != rhs.fontBold) {
        return true;
    }
    return false;
}

size_t Utf8CodePointCount(const std::string &text) {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            i += 1;
        } else if ((lead >> 5) == 0x6) {
            i += 2;
        } else if ((lead >> 4) == 0xE) {
            i += 3;
        } else if ((lead >> 3) == 0x1E) {
            i += 4;
        } else {
            i += 1;
        }
        count += 1;
    }
    return count;
}

void MeasureTextBox(const std::string &text, const std::string &family, const std::string &fontStyle,
                    float fontSize, float *ascentOut, float *widthOut) {
    *ascentOut = fontSize * 0.8f;
    *widthOut = fontSize * static_cast<float>(Utf8CodePointCount(text)) * 0.5f;
    std::shared_ptr<tgfx::Typeface> typeface = tgfx::Typeface::MakeFromName(family, fontStyle);
    if (!typeface) {
        typeface = tgfx::Typeface::MakeFromName("PingFang SC", fontStyle);
    }
    if (!typeface) {
        return;
    }
    const tgfx::Font font(typeface, fontSize);
    const tgfx::FontMetrics metrics = font.getMetrics();
    float ascent = metrics.ascent;
    if (ascent < 0.f) {
        ascent = -ascent;
    }
    if (ascent > 0.f) {
        *ascentOut = ascent;
    }
    const std::shared_ptr<tgfx::TextBlob> blob = tgfx::TextBlob::MakeFrom(text, font);
    if (blob) {
        *widthOut = blob->getBounds().width();
    }
}

void AddTextDiagnostic(SvgLayerTree &tree, const std::string &code, const std::string &message,
                       const std::string &nodeName) {
    Diagnostic diagnostic = {};
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.nodeName = nodeName;
    tree.diagnostics.push_back(diagnostic);
}

float ResolveFirstLength(const std::vector<tgfx::SVGLength> &values,
                         tgfx::SVGLengthContext::LengthType type,
                         const tgfx::SVGLengthContext &lengthContext, float fallback, bool *multi) {
    if (values.empty()) {
        return fallback;
    }
    if (values.size() > 1) {
        *multi = true;
    }
    return lengthContext.resolve(values.front(), type);
}

struct CollectState {
    SvgLayerTree *tree = nullptr;
    tgfx::SVGLengthContext lengthContext = tgfx::SVGLengthContext(tgfx::Size::Make(0.f, 0.f));
    TextRun current = {};
    bool hasCurrent = false;
    std::vector<TextRun> runs;
};

void FlushRun(CollectState &state) {
    if (!state.hasCurrent) {
        return;
    }
    if (!IsBlank(state.current.text)) {
        state.runs.push_back(state.current);
    }
    state.current = {};
    state.hasCurrent = false;
}

void AppendLiteral(CollectState &state, const std::string &text, const ComputedStyle &style,
                   const TextPosition &position, const tgfx::SVGTextContainer *transformNode) {
    if (state.hasCurrent && TextStylesDiffer(state.current.style, style)) {
        FlushRun(state);
    }
    if (!state.hasCurrent) {
        state.current.text = text;
        state.current.style = style;
        state.current.position = position;
        state.current.transformNode = transformNode;
        state.hasCurrent = true;
        return;
    }
    state.current.text += text;
}

void CollectFragment(const tgfx::SVGTextFragment &fragment, const ComputedStyle &parentStyle,
                     const TextPosition &inherited, const tgfx::SVGTextContainer *inheritedNode,
                     CollectState &state);

void CollectContainer(const tgfx::SVGTextContainer &container, const ComputedStyle &style,
                      const TextPosition &inherited, CollectState &state) {
    bool multi = false;
    TextPosition position = inherited;
    position.x = ResolveFirstLength(container.getX(), tgfx::SVGLengthContext::LengthType::Horizontal,
                                    state.lengthContext, inherited.x, &multi);
    position.y = ResolveFirstLength(container.getY(), tgfx::SVGLengthContext::LengthType::Vertical,
                                    state.lengthContext, inherited.y, &multi);
    position.dx =
        ResolveFirstLength(container.getDx(), tgfx::SVGLengthContext::LengthType::Horizontal,
                           state.lengthContext, inherited.dx, &multi);
    position.dy = ResolveFirstLength(container.getDy(), tgfx::SVGLengthContext::LengthType::Vertical,
                                     state.lengthContext, inherited.dy, &multi);
    if (multi) {
        AddTextDiagnostic(*state.tree, "text.glyphPositions",
                          "per-glyph x/y is imported from the first value only", "text");
    }
    if (!container.getRotate().empty()) {
        AddTextDiagnostic(*state.tree, "text.rotate", "glyph rotate is not imported", "text");
    }
    const bool ownPosition = !container.getX().empty() || !container.getY().empty();
    if (ownPosition && state.hasCurrent) {
        FlushRun(state);
    }
    for (const auto &child : container.getTextChildren()) {
        if (child) {
            CollectFragment(*child, style, position, &container, state);
        }
    }
}

void CollectFragment(const tgfx::SVGTextFragment &fragment, const ComputedStyle &parentStyle,
                     const TextPosition &inherited, const tgfx::SVGTextContainer *inheritedNode,
                     CollectState &state) {
    const ComputedStyle style = ResolveStyle(fragment, parentStyle, state.lengthContext);
    if (style.displayNone) {
        return;
    }
    const tgfx::SVGTag tag = fragment.tag();
    if (tag == tgfx::SVGTag::TextPath) {
        AddTextDiagnostic(*state.tree, "textPath.skipped", "textPath is not imported", "textPath");
        return;
    }
    if (tag == tgfx::SVGTag::TextLiteral) {
        const auto &literal = static_cast<const tgfx::SVGTextLiteral &>(fragment);
        AppendLiteral(state, literal.getText(), style, inherited, inheritedNode);
        return;
    }
    if (tag == tgfx::SVGTag::Text || tag == tgfx::SVGTag::TSpan) {
        CollectContainer(static_cast<const tgfx::SVGTextContainer &>(fragment), style, inherited,
                         state);
    }
}

TextAlign MapAlign(const std::string &anchor) {
    if (anchor == "middle") {
        return TextAlign::Center;
    }
    if (anchor == "end") {
        return TextAlign::Right;
    }
    return TextAlign::Left;
}

float AlignX(TextAlign align, float width) {
    if (align == TextAlign::Center) {
        return width * 0.5f;
    }
    if (align == TextAlign::Right) {
        return width;
    }
    return 0.f;
}

void EmitRun(const TextRun &run, EntityId parentId, SvgLayerTree &tree,
             const tgfx::SVGIDMapper *mapper) {
    std::string family = FirstFontFamily(run.style.fontFamily);
    bool fallback = false;
    if (family.empty()) {
        family = "PingFang SC";
        fallback = true;
    }
    const std::string fontStyle = MappedFontStyle(run.style);
    float ascent = 0.f;
    float width = 0.f;
    MeasureTextBox(run.text, family, fontStyle, run.style.fontSize, &ascent, &width);
    const TextAlign align = MapAlign(run.style.textAnchor);
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->name = "Text";
    layer->parentId = parentId;
    layer->visible = run.style.visible;
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->text.setStaticValue(run.text);
    content->fontFamily = family;
    content->fontStyle = fontStyle;
    content->fontSize = run.style.fontSize;
    content->boxTextMode = false;
    content->align = align;
    content->size = {width, ascent + run.style.fontSize * 0.2f};
    ApplyPaintStyles(*layer, run.style, mapper, {0.f, 0.f}, content->size, &tree.diagnostics);
    tgfx::Matrix matrix = tgfx::Matrix::MakeTrans(run.position.x + run.position.dx - AlignX(align, width),
                                                  run.position.y + run.position.dy - ascent);
    if (run.transformNode != nullptr) {
        matrix = run.transformNode->getTransform() * matrix;
    }
    ApplySvgMatrixToLayer(*layer, matrix);
    if (run.transformNode != nullptr) {
        const auto &opacity = run.transformNode->getOpacity();
        if (opacity.isValue()) {
            layer->transform.opacity.setStaticValue(*opacity);
        }
    }
    if (fallback) {
        AddTextDiagnostic(tree, "font.fallback", "font-family fell back to PingFang SC",
                          layer->name);
    }
    tree.layers.push_back(std::move(layer));
}

}  // namespace

void ImportSvgText(const tgfx::SVGText &text, EntityId parentId, SvgLayerTree &tree,
                   const tgfx::SVGLengthContext &lengthContext, const tgfx::SVGIDMapper *mapper,
                   const ComputedStyle &parentStyle) {
    CollectState state = {};
    state.tree = &tree;
    state.lengthContext = lengthContext;
    CollectFragment(text, parentStyle, {}, nullptr, state);
    FlushRun(state);
    for (const TextRun &run : state.runs) {
        EmitRun(run, parentId, tree, mapper);
    }
}

}  // namespace svg
}  // namespace motion

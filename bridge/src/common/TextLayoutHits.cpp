#include "TextLayoutHits.h"

#if defined(__APPLE__)

#include <memory>

#include "MotionStudio/textlayout/TextLayout.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxTextTypeface.h"

#endif

namespace bridge {

void ApplyAutoHeightTextHitSizes(motion::SceneState &state) {
#if defined(__APPLE__)
    for (motion::EvaluatedLayer &layer : state.layers) {
        if (!layer.textItem.has_value()) {
            continue;
        }
        motion::EvaluatedTextItem &text = *layer.textItem;
        if (!text.autoHeight) {
            continue;
        }
        if (text.containerSize.x <= 0.0f) {
            continue;
        }

        std::shared_ptr<tgfx::Typeface> typeface =
            motion::ResolveTextTypeface(text.fontAbsolutePath, text.fontFamily);
        if (typeface == nullptr) {
            continue;
        }

        motion::TgfxGlyphMetrics glyphMetrics(typeface);
        motion::textlayout::TextLayoutInput input;
        input.text = text.text;
        input.boxWidth = text.containerSize.x;
        input.fontSize = text.fontSize > 0.0f ? text.fontSize : 1.0f;
        switch (text.align) {
            case motion::TextAlign::Left: {
                input.align = motion::textlayout::Align::Left;
                break;
            }
            case motion::TextAlign::Center: {
                input.align = motion::textlayout::Align::Center;
                break;
            }
            case motion::TextAlign::Right: {
                input.align = motion::textlayout::Align::Right;
                break;
            }
        }
        input.metrics = &glyphMetrics;
        const motion::textlayout::TextLayoutResult layout = motion::textlayout::LayoutText(input);
        text.hitSize = layout.measuredSize;
    }
#else
    (void)state;
#endif
}

}  // namespace bridge

#include "motionstudio_bridge.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/SetTextAlignCommand.h"
#include "MotionStudio/undo/SetTextBoxTextModeCommand.h"
#include "MotionStudio/undo/SetTextFontCommand.h"
#include "MotionStudio/undo/SetTextFontSizeCommand.h"
#include "MotionStudio/undo/SetTextSizeCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

#if defined(__APPLE__)
#include "MeasurePointTextSize.h"
#endif

using namespace bridge;

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::TextAlign;
using motion::TextContent;
using motion::Vec2;

namespace {

TextContent *TextContentOf(Layer *layer) {
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return nullptr;
    }
    return static_cast<TextContent *>(layer->content.get());
}

}  // namespace

uint64_t ms_command_add_text_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Text);
    layer->name = "Text " + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.anchorPoint.setStaticValue(Vec2{200.0f, 60.0f});
    layer->transform.position.setStaticValue(
        Vec2{composition->width * 0.5f, composition->height * 0.5f});
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(motion::Color{0.0f, 0.0f, 0.0f, 1.0f});
    layer->styles.push_back(std::move(fill));
    const uint64_t layerId = layer->id.value;
    Execute(document, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

bool ms_command_set_text_font(MSDocument *document, uint64_t layerId, const char *family,
                              const char *style) {
    DocumentLock lock(document);
    if (document == nullptr || family == nullptr || style == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextFontCommand>(EntityId{layerId}, std::string(family), std::string(style)));
    return true;
}

bool ms_command_set_text_box_text_mode(MSDocument *document, uint64_t layerId, bool boxTextMode,
                                       int64_t frame) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    if (content == nullptr) {
        return false;
    }

    std::optional<Vec2> sizeWhenEnabling;
    if (boxTextMode && !content->boxTextMode) {
#if defined(__APPLE__)
        const std::string text = content->text.evaluate(frame);
        sizeWhenEnabling = MeasurePointTextSize(text, content->fontSize, content->align,
                                                content->fontFamily, content->fontStyle);
#else
        (void)frame;
        sizeWhenEnabling = content->size;
#endif
    }

    Execute(document, std::make_unique<motion::SetTextBoxTextModeCommand>(EntityId{layerId}, boxTextMode, sizeWhenEnabling));
    return true;
}

bool ms_command_set_text_font_size(MSDocument *document, uint64_t layerId, float fontSize) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextFontSizeCommand>(EntityId{layerId}, fontSize));
    return true;
}

bool ms_command_set_text_size(MSDocument *document, uint64_t layerId, float width, float height) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextSizeCommand>(EntityId{layerId}, Vec2{std::max(1.0f, width), std::max(1.0f, height)}));
    return true;
}

bool ms_command_set_text_align(MSDocument *document, uint64_t layerId, MS_TEXT_ALIGN align) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    if (align < MS_TEXT_ALIGN_LEFT || align > MS_TEXT_ALIGN_RIGHT) {
        return false;
    }
    if (TextContentOf(FindLayer(document, layerId)) == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<motion::SetTextAlignCommand>(EntityId{layerId}, static_cast<TextAlign>(align)));
    return true;
}

bool ms_layer_text_box_text_mode(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content != nullptr ? content->boxTextMode : false;
}

float ms_layer_text_font_size(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content != nullptr ? content->fontSize : 0.0f;
}

bool ms_layer_text_size(MSDocument *document, uint64_t layerId, float *width, float *height) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    if (content == nullptr) {
        return false;
    }
    if (width != nullptr) {
        *width = content->size.x;
    }
    if (height != nullptr) {
        *height = content->size.y;
    }
    return true;
}

MS_TEXT_ALIGN ms_layer_text_align(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    if (content == nullptr) {
        return MS_TEXT_ALIGN_LEFT;
    }
    return static_cast<MS_TEXT_ALIGN>(content->align);
}

char *ms_layer_text_font_family(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content == nullptr ? nullptr : strdup(content->fontFamily.c_str());
}

char *ms_layer_text_font_style(MSDocument *document, uint64_t layerId) {
    DocumentLock lock(document);
    TextContent *content = TextContentOf(FindLayer(document, layerId));
    return content == nullptr ? nullptr : strdup(content->fontStyle.c_str());
}

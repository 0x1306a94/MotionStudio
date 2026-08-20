#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/AddGradientStopCommand.h"
#include "MotionStudio/undo/RemoveGradientStopCommand.h"
#include "MotionStudio/undo/SetGradientTypeCommand.h"
#include "MotionStudio/undo/SetStylePaintModeCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::AddGradientStopCommand;
using motion::Color;
using motion::Document;
using motion::EntityId;
using motion::FillStyle;
using motion::FindShader;
using motion::GradientType;
using motion::Layer;
using motion::LayerStyleType;
using motion::RemoveGradientStopCommand;
using motion::Serializer;
using motion::SetGradientTypeCommand;
using motion::SetStylePaintModeCommand;
using motion::StrokeStyle;
using motion::StylePaintMode;

namespace {

StylePaintMode ToStylePaintMode(MS_PAINT_MODE mode) {
    switch (mode) {
        case MS_PAINT_MODE_COLOR: {
            return StylePaintMode::Color;
        }
        case MS_PAINT_MODE_SHADER: {
            return StylePaintMode::Shader;
        }
        case MS_PAINT_MODE_GRADIENT: {
            return StylePaintMode::Gradient;
        }
        case MS_PAINT_MODE_INVALID: {
            break;
        }
    }
    return StylePaintMode::Color;
}

GradientType ToGradientType(MS_GRADIENT_TYPE type) {
    switch (type) {
        case MS_GRADIENT_TYPE_LINEAR: {
            return GradientType::Linear;
        }
        case MS_GRADIENT_TYPE_RADIAL: {
            return GradientType::Radial;
        }
        case MS_GRADIENT_TYPE_CONIC: {
            return GradientType::Conic;
        }
        case MS_GRADIENT_TYPE_DIAMOND: {
            return GradientType::Diamond;
        }
        case MS_GRADIENT_TYPE_INVALID: {
            break;
        }
    }
    return GradientType::Linear;
}

MS_GRADIENT_TYPE FromGradientType(GradientType type) {
    return static_cast<MS_GRADIENT_TYPE>(type);
}

motion::GradientPaint *StyleGradient(motion::LayerStyle *style) {
    if (style->type() == LayerStyleType::Fill) {
        return &static_cast<FillStyle *>(style)->gradient;
    }
    if (style->type() == LayerStyleType::Stroke) {
        return &static_cast<StrokeStyle *>(style)->gradient;
    }
    return nullptr;
}

}  // namespace

bool ms_document_set_style_paint_mode(MSDocument *document, uint64_t layerId, int index,
                                      MS_PAINT_MODE mode, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr || mode == MS_PAINT_MODE_INVALID) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    const StylePaintMode paintMode = ToStylePaintMode(mode);
    if (paintMode == StylePaintMode::Shader) {
        EntityId requested{shaderId};
        if (requested.isValid() && FindShader(*document->document, requested) == nullptr) {
            return false;
        }
        if (!requested.isValid() && document->document->shaders.empty()) {
            return false;
        }
    }
    Execute(document, std::make_unique<SetStylePaintModeCommand>(EntityId{layerId}, index, paintMode, EntityId{shaderId}));
    return true;
}

MS_GRADIENT_TYPE ms_layer_style_gradient_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_GRADIENT_TYPE_INVALID;
    }
    motion::GradientPaint *gradient = StyleGradient(layer->styles[static_cast<size_t>(index)].get());
    if (gradient == nullptr) {
        return MS_GRADIENT_TYPE_INVALID;
    }
    return FromGradientType(gradient->type);
}

int ms_layer_style_gradient_stop_count(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return 0;
    }
    motion::GradientPaint *gradient = StyleGradient(layer->styles[static_cast<size_t>(index)].get());
    if (gradient == nullptr) {
        return 0;
    }
    return static_cast<int>(gradient->stops.size());
}

bool ms_document_set_gradient_type(MSDocument *document, uint64_t layerId, int index,
                                   MS_GRADIENT_TYPE type) {
    DocumentLock lock(document);
    if (document == nullptr || type == MS_GRADIENT_TYPE_INVALID) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<SetGradientTypeCommand>(EntityId{layerId}, index, ToGradientType(type)));
    return true;
}

bool ms_document_add_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                   int insertIndex, float r, float g, float b, float a,
                                   float position) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<AddGradientStopCommand>(EntityId{layerId}, index, insertIndex, Color{r, g, b, a}, position));
    return true;
}

bool ms_document_remove_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                      int stopIndex) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<RemoveGradientStopCommand>(EntityId{layerId}, index, stopIndex));
    return true;
}

#include "motionstudio_bridge.h"

#include <cstdlib>
#include <string>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Composition;
using motion::FillStyle;
using motion::Layer;
using motion::StrokeStyle;

/* ============================ layer queries ============================ */

uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || index < 0 || static_cast<size_t>(index) >= composition->layers.size()) {
        return 0;
    }
    return composition->layers[static_cast<size_t>(index)]->id.value;
}

char *ms_layer_name(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? strdup(layer->name.c_str()) : nullptr;
}

MS_LAYER ms_layer_type(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return MS_LAYER_INVALID;
    }
    return static_cast<MS_LAYER>(layer->type());
}

int64_t ms_layer_in_point(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->inPoint : 0;
}

int64_t ms_layer_out_point(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->outPoint : 0;
}

uint64_t ms_layer_parent_id(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? layer->parentId.value : 0;
}

bool ms_layer_visible(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr && layer->visible;
}

bool ms_layer_locked(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr && layer->locked;
}

bool ms_layer_effectively_visible(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    motion::Document *doc = Doc(document);
    return layer != nullptr && doc != nullptr && layer->isEffectivelyVisible(*doc);
}

bool ms_layer_effectively_locked(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    motion::Document *doc = Doc(document);
    return layer != nullptr && doc != nullptr && layer->isEffectivelyLocked(*doc);
}

/* ============================ layer style queries ============================ */

int ms_layer_style_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->styles.size()) : 0;
}

MS_STYLE ms_layer_style_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_STYLE_INVALID;
    }
    switch (layer->styles[static_cast<size_t>(index)]->type()) {
        case motion::LayerStyleType::Fill: {
            return MS_STYLE_FILL;
        }
        case motion::LayerStyleType::Stroke: {
            return MS_STYLE_STROKE;
        }
    }
    return MS_STYLE_INVALID;
}

int ms_layer_effect_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->effects.size()) : 0;
}

MS_EFFECT ms_layer_effect_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->effects.size()) {
        return MS_EFFECT_INVALID;
    }
    switch (layer->effects[static_cast<size_t>(index)]->type()) {
        case motion::LayerEffectType::BrightnessContrast: {
            return MS_EFFECT_BRIGHTNESS_CONTRAST;
        }
        case motion::LayerEffectType::GaussianBlur: {
            return MS_EFFECT_GAUSSIAN_BLUR;
        }
    }
    return MS_EFFECT_INVALID;
}

bool ms_layer_effect_enabled_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->effects.size()) {
        return false;
    }
    return layer->effects[static_cast<size_t>(index)]->enabled;
}

bool ms_layer_effect_repeat_edge_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->effects.size()) {
        return false;
    }
    const motion::LayerEffect &effect = *layer->effects[static_cast<size_t>(index)];
    if (effect.type() != motion::LayerEffectType::GaussianBlur) {
        return false;
    }
    return static_cast<const motion::GaussianBlurEffect &>(effect).repeatEdgePixels;
}

int ms_layer_fx_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->layerStyles.size()) : 0;
}

MS_LAYER_FX ms_layer_fx_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return MS_LAYER_FX_INVALID;
    }
    switch (layer->layerStyles[static_cast<size_t>(index)]->type()) {
        case motion::LayerFxType::DropShadow: {
            return MS_LAYER_FX_DROP_SHADOW;
        }
        case motion::LayerFxType::OuterGlow: {
            return MS_LAYER_FX_OUTER_GLOW;
        }
        case motion::LayerFxType::Stroke: {
            return MS_LAYER_FX_STROKE;
        }
    }
    return MS_LAYER_FX_INVALID;
}

bool ms_layer_fx_enabled_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return false;
    }
    return layer->layerStyles[static_cast<size_t>(index)]->enabled;
}

MS_BLEND ms_layer_fx_blend_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return MS_BLEND_NORMAL;
    }
    const motion::LayerFx &style = *layer->layerStyles[static_cast<size_t>(index)];
    switch (style.type()) {
        case motion::LayerFxType::DropShadow: {
            return static_cast<MS_BLEND>(static_cast<const motion::DropShadowStyle &>(style).blendMode);
        }
        case motion::LayerFxType::OuterGlow: {
            return static_cast<MS_BLEND>(static_cast<const motion::OuterGlowStyle &>(style).blendMode);
        }
        case motion::LayerFxType::Stroke: {
            return static_cast<MS_BLEND>(static_cast<const motion::LayerStrokeStyle &>(style).blendMode);
        }
    }
    return MS_BLEND_NORMAL;
}

MS_STROKE_POSITION ms_layer_fx_stroke_position_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return MS_STROKE_POSITION_INVALID;
    }
    const motion::LayerFx &style = *layer->layerStyles[static_cast<size_t>(index)];
    if (style.type() != motion::LayerFxType::Stroke) {
        return MS_STROKE_POSITION_INVALID;
    }
    return static_cast<MS_STROKE_POSITION>(static_cast<const motion::LayerStrokeStyle &>(style).position);
}

MS_BLEND ms_layer_blend_mode(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return MS_BLEND_INVALID;
    }
    return static_cast<MS_BLEND>(layer->blendMode);
}

MS_BLEND ms_layer_style_blend_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_BLEND_INVALID;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case motion::LayerStyleType::Fill: {
            return static_cast<MS_BLEND>(static_cast<FillStyle *>(style)->blendMode);
        }
        case motion::LayerStyleType::Stroke: {
            return static_cast<MS_BLEND>(static_cast<StrokeStyle *>(style)->blendMode);
        }
    }
    return MS_BLEND_INVALID;
}

MS_STROKE_POSITION ms_layer_style_stroke_position_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_STROKE_POSITION_INVALID;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    if (style->type() != motion::LayerStyleType::Stroke) {
        return MS_STROKE_POSITION_INVALID;
    }
    return static_cast<MS_STROKE_POSITION>(static_cast<StrokeStyle *>(style)->position);
}

/* ============================ mask / track matte queries ============================ */

int ms_layer_mask_count(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? static_cast<int>(layer->masks.size()) : 0;
}

MS_MASK ms_layer_mask_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->masks.size()) {
        return MS_MASK_INVALID;
    }
    return static_cast<MS_MASK>(layer->masks[static_cast<size_t>(index)].mode);
}

bool ms_layer_mask_inverted_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->masks.size()) {
        return false;
    }
    return layer->masks[static_cast<size_t>(index)].inverted;
}

MS_TRACK_MATTE ms_layer_track_matte_type(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return MS_TRACK_MATTE_NONE;
    }
    return static_cast<MS_TRACK_MATTE>(layer->trackMatteType);
}

uint64_t ms_layer_track_matte_layer_id(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return 0;
    }
    return layer->trackMatteLayerId.value;
}

bool ms_layer_follow_path_enabled(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return false;
    }
    return layer->followPath.enabled;
}

uint64_t ms_layer_follow_path_layer_id(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return 0;
    }
    return layer->followPath.pathLayerId.value;
}

bool ms_layer_follow_path_orient(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return true;
    }
    return layer->followPath.orientAlongPath;
}

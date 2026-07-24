#include "motionstudio_bridge.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/MoveKeyframeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/RemoveKeyframeCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/SetCompositionBackgroundColorCommand.h"
#include "MotionStudio/undo/SetCompositionCornerRadiusCommand.h"
#include "MotionStudio/undo/SetCompositionSettingsCommand.h"
#include "MotionStudio/undo/SetEasingCommand.h"
#include "MotionStudio/undo/SetLayerLockedCommand.h"
#include "MotionStudio/undo/SetLayerVisibleCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/UndoManager.h"

#include "DocumentLock.h"
#include "MSDocument.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::Serializer;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapeFill;
using motion::ShapeRect;
using motion::UndoManager;
using motion::Vec2;

namespace {

Document *Doc(MSDocument *handle) {
    return handle != nullptr ? handle->document.get() : nullptr;
}

Composition *FindComposition(MSDocument *handle, uint64_t compositionId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    for (auto &composition : document->compositions) {
        if (composition->id.value == compositionId) {
            return composition.get();
        }
    }
    return nullptr;
}

Layer *FindLayer(MSDocument *handle, uint64_t layerId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    return document->entityIndex().findLayer(EntityId{layerId});
}

AnimatableBase *FindProperty(MSDocument *handle, uint64_t entityId, const char *path) {
    Document *document = Doc(handle);
    if (document == nullptr || path == nullptr) {
        return nullptr;
    }
    return ResolveAnimatable(*document, PropertyPath{EntityId{entityId}, path});
}

// Downcasts via valueType() (dynamic_cast is banned by the coding rules).
const Animatable<float> *AsFloat(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Float) {
        return nullptr;
    }
    return static_cast<const Animatable<float> *>(base);
}

const Animatable<Vec2> *AsVec2(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Vec2) {
        return nullptr;
    }
    return static_cast<const Animatable<Vec2> *>(base);
}

const Animatable<Color> *AsColor(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Color) {
        return nullptr;
    }
    return static_cast<const Animatable<Color> *>(base);
}

// Builds a fully-initialized keyframe (avoids partial aggregate init warnings).
template <typename T>
Keyframe<T> MakeKeyframe(FrameTime time, T value) {
    Keyframe<T> keyframe;
    keyframe.time = time;
    keyframe.value = std::move(value);
    return keyframe;
}

void Execute(MSDocument *handle, std::unique_ptr<motion::Command> command) {
    if (handle == nullptr) {
        return;
    }
    handle->undoManager->execute(*handle->document, std::move(command));
}

PropertyPath MakePath(uint64_t entityId, const char *path) {
    return PropertyPath{EntityId{entityId}, path != nullptr ? path : ""};
}

Easing MakeEasing(int easingType, float inX, float inY, float outX, float outY) {
    switch (easingType) {
        case MS_EASING_BEZIER: {
            return Easing::Bezier(inX, inY, outX, outY);
        }
        case MS_EASING_HOLD: {
            return Easing::Hold();
        }
        default: {
            return Easing::Linear();
        }
    }
}

const Color SHAPE_PALETTE[6] = {
    {0.29f, 0.56f, 0.89f, 1.0f},
    {0.91f, 0.52f, 0.29f, 1.0f},
    {0.40f, 0.76f, 0.45f, 1.0f},
    {0.69f, 0.42f, 0.87f, 1.0f},
    {0.96f, 0.71f, 0.25f, 1.0f},
    {0.90f, 0.38f, 0.45f, 1.0f},
};

uint64_t AddShapeLayer(MSDocument *handle, uint64_t compositionId, bool ellipse) {
    Composition *composition = FindComposition(handle, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = (ellipse ? "Ellipse " : "Rectangle ") +
        std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(
        Vec2{composition->width * 0.5f, composition->height * 0.5f});

    auto *content = static_cast<ShapeContent *>(layer->content.get());
    if (ellipse) {
        auto shape = std::make_unique<ShapeEllipse>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->elements.push_back(std::move(shape));
    } else {
        auto shape = std::make_unique<ShapeRect>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->elements.push_back(std::move(shape));
    }
    auto fill = std::make_unique<ShapeFill>();
    fill->color.setStaticValue(SHAPE_PALETTE[composition->layers.size() % 6]);
    content->elements.push_back(std::move(fill));

    const uint64_t layerId = layer->id.value;
    Execute(handle,
            std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

}  // namespace

/* ============================ lifecycle ============================ */

MSDocument *ms_document_create(void) {
    auto *handle = new MSDocument();
    handle->document = std::make_unique<Document>();
    handle->undoManager = std::make_unique<UndoManager>();

    auto composition = std::make_unique<Composition>();
    composition->name = "Composition 1";
    composition->duration = 150;  // 5 seconds at the default 30 fps
    handle->document->addComposition(std::move(composition));
    return handle;
}

MSDocument *ms_document_load(const char *jsonText, size_t length, char **errorOut) {
    if (jsonText == nullptr) {
        return nullptr;
    }
    auto result = Serializer::deserialize(std::string(jsonText, length));
    if (!result.hasValue()) {
        if (errorOut != nullptr) {
            *errorOut = strdup(result.error().c_str());
        }
        return nullptr;
    }
    auto *handle = new MSDocument();
    handle->document = std::move(result).value();
    return handle;
}

void ms_document_destroy(MSDocument *document) {
    // No lock: destroying implies unique ownership of the handle.
    delete document;
}

char *ms_document_save(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return nullptr;
    }
    return strdup(Serializer::serialize(*document->document).c_str());
}

void ms_string_free(char *string) {
    free(string);
}

/* ============================ undo / redo ============================ */

bool ms_document_undo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return false;
    }
    document->undoManager->undo(*document->document);
    return true;
}

bool ms_document_redo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return false;
    }
    document->undoManager->redo(*document->document);
    return true;
}

bool ms_document_can_undo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canUndo();
}

bool ms_document_can_redo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canRedo();
}

char *ms_document_undo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return nullptr;
    }
    return strdup(document->undoManager->undoDescription().c_str());
}

char *ms_document_redo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return nullptr;
    }
    return strdup(document->undoManager->redoDescription().c_str());
}

void ms_document_end_merge_group(MSDocument *document) {
    DocumentLock guard(document);
    if (document != nullptr) {
        document->undoManager->endMergeGroup();
    }
}

/* ============================ composition queries ============================ */

int ms_document_composition_count(MSDocument *document) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    return doc != nullptr ? int(doc->compositions.size()) : 0;
}

uint64_t ms_document_composition_id_at(MSDocument *document, int index) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr || index < 0 || size_t(index) >= doc->compositions.size()) {
        return 0;
    }
    return doc->compositions[size_t(index)]->id.value;
}

int64_t ms_composition_duration(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->duration : 0;
}

int ms_composition_width(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->width : 0;
}

int ms_composition_height(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->height : 0;
}

int ms_composition_frame_rate_num(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? int(composition->frameRate.num) : 0;
}

int ms_composition_frame_rate_den(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? int(composition->frameRate.den) : 0;
}

int ms_composition_layer_count(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? int(composition->layers.size()) : 0;
}

void ms_composition_background_color(MSDocument *document, uint64_t compositionId, float *r,
                                     float *g, float *b, float *a) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    if (r != nullptr) {
        *r = composition->backgroundColor.r;
    }
    if (g != nullptr) {
        *g = composition->backgroundColor.g;
    }
    if (b != nullptr) {
        *b = composition->backgroundColor.b;
    }
    if (a != nullptr) {
        *a = composition->backgroundColor.a;
    }
}

float ms_composition_corner_radius(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? composition->cornerRadius : 0.0f;
}

char *ms_composition_name(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    return composition != nullptr ? strdup(composition->name.c_str()) : nullptr;
}

/* ============================ layer queries ============================ */

uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || index < 0 || size_t(index) >= composition->layers.size()) {
        return 0;
    }
    return composition->layers[size_t(index)]->id.value;
}

char *ms_layer_name(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    return layer != nullptr ? strdup(layer->name.c_str()) : nullptr;
}

int ms_layer_type(MSDocument *document, uint64_t layerId) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr) {
        return -1;
    }
    return int(layer->type());
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

/* ============================ property queries ============================ */

int ms_property_type(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    return property != nullptr ? int(property->valueType()) : -1;
}

bool ms_property_is_animated(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr) {
        return false;
    }
    switch (property->valueType()) {
        case AnimatableType::Float: {
            return AsFloat(property)->isAnimated();
        }
        case AnimatableType::Vec2: {
            return AsVec2(property)->isAnimated();
        }
        case AnimatableType::Color: {
            return AsColor(property)->isAnimated();
        }
        case AnimatableType::BezierPath: {
            return static_cast<const Animatable<motion::BezierPath> *>(property)->isAnimated();
        }
        case AnimatableType::String: {
            return static_cast<const Animatable<std::string> *>(property)->isAnimated();
        }
    }
    return false;
}

float ms_property_static_float(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    const Animatable<float> *property = AsFloat(FindProperty(document, entityId, path));
    return property != nullptr ? property->staticValue() : 0.0f;
}

void ms_property_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float *x,
                             float *y) {
    DocumentLock guard(document);
    const Animatable<Vec2> *property = AsVec2(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Vec2 value = property->staticValue();
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

void ms_property_static_color(MSDocument *document, uint64_t entityId, const char *path, float *r, float *g, float *b, float *a) {
    DocumentLock guard(document);
    const Animatable<Color> *property = AsColor(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Color value = property->staticValue();
    if (r != nullptr) {
        *r = value.r;
    }
    if (g != nullptr) {
        *g = value.g;
    }
    if (b != nullptr) {
        *b = value.b;
    }
    if (a != nullptr) {
        *a = value.a;
    }
}

int ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr) {
        return 0;
    }
    switch (property->valueType()) {
        case AnimatableType::Float: {
            return int(AsFloat(property)->keyframes().size());
        }
        case AnimatableType::Vec2: {
            return int(AsVec2(property)->keyframes().size());
        }
        case AnimatableType::Color: {
            return int(AsColor(property)->keyframes().size());
        }
        case AnimatableType::BezierPath: {
            return int(
                static_cast<const Animatable<motion::BezierPath> *>(property)->keyframes().size());
        }
        case AnimatableType::String: {
            return int(
                static_cast<const Animatable<std::string> *>(property)->keyframes().size());
        }
    }
    return 0;
}

// Looks up the keyframe at index for the expected value type.
template <typename T>
const Keyframe<T> *KeyframeAt(const Animatable<T> *property, int index) {
    if (property == nullptr || index < 0 || size_t(index) >= property->keyframes().size()) {
        return nullptr;
    }
    return &property->keyframes()[size_t(index)];
}

int64_t ms_property_keyframe_time_at(MSDocument *document, uint64_t entityId, const char *path, int index) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    const Keyframe<float> *floatKey = KeyframeAt(AsFloat(property), index);
    if (floatKey != nullptr) {
        return floatKey->time;
    }
    const Keyframe<Vec2> *vec2Key = KeyframeAt(AsVec2(property), index);
    if (vec2Key != nullptr) {
        return vec2Key->time;
    }
    const Keyframe<Color> *colorKey = KeyframeAt(AsColor(property), index);
    if (colorKey != nullptr) {
        return colorKey->time;
    }
    return 0;
}

float ms_property_keyframe_float_at(MSDocument *document, uint64_t entityId, const char *path, int index) {
    DocumentLock guard(document);
    const Keyframe<float> *keyframe =
        KeyframeAt(AsFloat(FindProperty(document, entityId, path)), index);
    return keyframe != nullptr ? keyframe->value : 0.0f;
}

void ms_property_keyframe_vec2_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *x, float *y) {
    DocumentLock guard(document);
    const Keyframe<Vec2> *keyframe =
        KeyframeAt(AsVec2(FindProperty(document, entityId, path)), index);
    if (keyframe == nullptr) {
        return;
    }
    if (x != nullptr) {
        *x = keyframe->value.x;
    }
    if (y != nullptr) {
        *y = keyframe->value.y;
    }
}

int ms_property_keyframe_easing_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *inX, float *inY, float *outX, float *outY) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    const Easing *easing = nullptr;
    const Keyframe<float> *floatKey = KeyframeAt(AsFloat(property), index);
    if (floatKey != nullptr) {
        easing = &floatKey->easing;
    }
    const Keyframe<Vec2> *vec2Key = KeyframeAt(AsVec2(property), index);
    if (vec2Key != nullptr) {
        easing = &vec2Key->easing;
    }
    const Keyframe<Color> *colorKey = KeyframeAt(AsColor(property), index);
    if (colorKey != nullptr) {
        easing = &colorKey->easing;
    }
    if (easing == nullptr) {
        return -1;
    }
    if (inX != nullptr) {
        *inX = easing->inX;
    }
    if (inY != nullptr) {
        *inY = easing->inY;
    }
    if (outX != nullptr) {
        *outX = easing->outX;
    }
    if (outY != nullptr) {
        *outY = easing->outY;
    }
    return int(easing->type);
}

float ms_property_evaluate_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    const Animatable<float> *property = AsFloat(FindProperty(document, entityId, path));
    return property != nullptr ? property->evaluate(FrameTime(frame)) : 0.0f;
}

void ms_property_evaluate_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x, float *y) {
    DocumentLock guard(document);
    const Animatable<Vec2> *property = AsVec2(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Vec2 value = property->evaluate(FrameTime(frame));
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

/* ============================ commands ============================ */

void ms_command_set_static_float(MSDocument *document, uint64_t entityId, const char *path, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(value)));
}

void ms_command_set_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Vec2{x, y})));
}

void ms_command_set_static_color(MSDocument *document, uint64_t entityId, const char *path, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(Color{r, g, b, a})));
}

void ms_command_set_composition_background_color(MSDocument *document, uint64_t compositionId, float r, float g, float b, float a) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionBackgroundColorCommand>(EntityId{compositionId}, Color{r, g, b, a}));
}

void ms_command_set_composition_corner_radius(MSDocument *document, uint64_t compositionId, float cornerRadius) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetCompositionCornerRadiusCommand>(EntityId{compositionId}, cornerRadius));
}

void ms_command_set_composition_size(MSDocument *document, uint64_t compositionId, int width, int height) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = width;
    settings.height = height;
    settings.duration = composition->duration;
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_duration(MSDocument *document, uint64_t compositionId, int64_t duration) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = FrameTime(duration);
    settings.frameRate = composition->frameRate;
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_set_composition_frame_rate(MSDocument *document, uint64_t compositionId, int frameRateNum, int frameRateDen) {
    DocumentLock guard(document);
    Composition *composition = FindComposition(document, compositionId);
    if (composition == nullptr || frameRateNum <= 0 || frameRateDen <= 0) {
        return;
    }
    motion::CompositionSettings settings;
    settings.width = composition->width;
    settings.height = composition->height;
    settings.duration = composition->duration;
    settings.frameRate = {uint32_t(frameRateNum), uint32_t(frameRateDen)};
    Execute(document, std::make_unique<motion::SetCompositionSettingsCommand>(EntityId{compositionId}, settings));
}

void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float value) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(FrameTime(frame), value))));
}

void ms_command_add_keyframe_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float x, float y) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::AddKeyframeCommand>(MakePath(entityId, path), motion::KeyframeData(MakeKeyframe(FrameTime(frame), Vec2{x, y}))));
}

void ms_command_remove_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveKeyframeCommand>(MakePath(entityId, path), FrameTime(frame)));
}

void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path, int64_t oldFrame, int64_t newFrame) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveKeyframeCommand>(MakePath(entityId, path), FrameTime(oldFrame), FrameTime(newFrame)));
}

void ms_command_set_easing(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, int easingType, float inX, float inY, float outX, float outY) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetEasingCommand>(MakePath(entityId, path), FrameTime(frame), MakeEasing(easingType, inX, inY, outX, outY)));
}

uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, false);
}

uint64_t ms_command_add_ellipse_layer(MSDocument *document, uint64_t compositionId) {
    DocumentLock guard(document);
    return AddShapeLayer(document, compositionId, true);
}

void ms_command_remove_layer(MSDocument *document, uint64_t compositionId, uint64_t layerId) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::RemoveLayerCommand>(EntityId{compositionId}, EntityId{layerId}));
}

void ms_command_move_layer(MSDocument *document, uint64_t compositionId, int fromIndex, int toIndex) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::MoveLayerCommand>(EntityId{compositionId}, fromIndex, toIndex));
}

void ms_command_set_layer_visible(MSDocument *document, uint64_t layerId, bool visible) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerVisibleCommand>(EntityId{layerId}, visible));
}

void ms_command_set_layer_locked(MSDocument *document, uint64_t layerId, bool locked) {
    DocumentLock guard(document);
    Execute(document, std::make_unique<motion::SetLayerLockedCommand>(EntityId{layerId}, locked));
}

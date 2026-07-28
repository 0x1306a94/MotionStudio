#include "motionstudio_bridge.h"

#include <string>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Color;
using motion::Easing;
using motion::FrameTime;
using motion::Keyframe;
using motion::Vec2;

namespace {

template <typename T>
const Keyframe<T> *KeyframeAt(const Animatable<T> *property, int index) {
    if (property == nullptr || index < 0 || static_cast<size_t>(index) >= property->keyframes().size()) {
        return nullptr;
    }
    return &property->keyframes()[static_cast<size_t>(index)];
}

}  // namespace


/* ============================ property queries ============================ */

MS_VALUE ms_property_type(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    return property != nullptr ? static_cast<MS_VALUE>(property->valueType()) : MS_VALUE_INVALID;
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

void ms_property_static_vec2(MSDocument *document, uint64_t entityId, const char *path, float *x, float *y) {
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

MSBezierPath *ms_property_static_bezier_path(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    const Animatable<motion::BezierPath> *property = AsBezierPath(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(property->staticValue());
}

int ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path) {
    DocumentLock guard(document);
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr) {
        return 0;
    }
    switch (property->valueType()) {
        case AnimatableType::Float: {
            return static_cast<int>(AsFloat(property)->keyframes().size());
        }
        case AnimatableType::Vec2: {
            return static_cast<int>(AsVec2(property)->keyframes().size());
        }
        case AnimatableType::Color: {
            return static_cast<int>(AsColor(property)->keyframes().size());
        }
        case AnimatableType::BezierPath: {
            return static_cast<int>(static_cast<const Animatable<motion::BezierPath> *>(property)->keyframes().size());
        }
        case AnimatableType::String: {
            return static_cast<int>(static_cast<const Animatable<std::string> *>(property)->keyframes().size());
        }
    }
    return 0;
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
    const Keyframe<float> *keyframe = KeyframeAt(AsFloat(FindProperty(document, entityId, path)), index);
    return keyframe != nullptr ? keyframe->value : 0.0f;
}

void ms_property_keyframe_vec2_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *x, float *y) {
    DocumentLock guard(document);
    const Keyframe<Vec2> *keyframe = KeyframeAt(AsVec2(FindProperty(document, entityId, path)), index);
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

MS_EASING ms_property_keyframe_easing_at(MSDocument *document, uint64_t entityId, const char *path, int index, float *inX, float *inY, float *outX, float *outY) {
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
        return MS_EASING_INVALID;
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
    return static_cast<MS_EASING>(easing->type);
}

float ms_property_evaluate_float(MSDocument *document, uint64_t entityId, const char *path, int64_t frame) {
    DocumentLock guard(document);
    const Animatable<float> *property = AsFloat(FindProperty(document, entityId, path));
    return property != nullptr ? property->evaluate(static_cast<FrameTime>(frame)) : 0.0f;
}

void ms_property_evaluate_vec2(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *x, float *y) {
    DocumentLock guard(document);
    const Animatable<Vec2> *property = AsVec2(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Vec2 value = property->evaluate(static_cast<FrameTime>(frame));
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

void ms_property_evaluate_color(MSDocument *document, uint64_t entityId, const char *path, int64_t frame, float *r, float *g, float *b, float *a) {
    DocumentLock guard(document);
    const Animatable<Color> *property = AsColor(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return;
    }
    const Color value = property->evaluate(static_cast<FrameTime>(frame));
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

MSBezierPath *ms_property_evaluate_bezier_path(MSDocument *document, uint64_t entityId, const char *path,
                                               int64_t frame) {
    DocumentLock guard(document);
    const Animatable<motion::BezierPath> *property = AsBezierPath(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(property->evaluate(static_cast<FrameTime>(frame)));
}

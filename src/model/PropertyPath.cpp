#include "MotionStudio/model/PropertyPath.h"

#include <cctype>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeTrimPath.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

bool PropertyPath::operator==(const PropertyPath &other) const {
    return entityId == other.entityId && path == other.path;
}

bool PropertyPath::operator!=(const PropertyPath &other) const {
    return !(*this == other);
}

bool PathSegment::operator==(const PathSegment &other) const {
    return name == other.name && index == other.index;
}

std::vector<PathSegment> ParsePropertyPath(const std::string &path) {
    std::vector<PathSegment> segments;
    size_t position = 0;
    while (position < path.size()) {
        PathSegment segment;

        const size_t nameStart = position;
        while (position < path.size() && path[position] != '.' && path[position] != '[') {
            ++position;
        }
        segment.name = path.substr(nameStart, position - nameStart);
        if (segment.name.empty()) {
            return {};
        }

        if (position < path.size() && path[position] == '[') {
            ++position;
            int index = 0;
            bool hasDigit = false;
            while (position < path.size() &&
                   std::isdigit(static_cast<unsigned char>(path[position]))) {
                index = index * 10 + (path[position] - '0');
                hasDigit = true;
                ++position;
            }
            if (!hasDigit || position >= path.size() || path[position] != ']') {
                return {};
            }
            ++position;
            segment.index = index;
        }

        segments.push_back(segment);
        if (position >= path.size()) {
            break;
        }
        if (path[position] != '.') {
            return {};
        }
        ++position;
        if (position >= path.size()) {
            return {};  // Reject trailing '.'.
        }
    }
    return segments;
}

namespace {

AnimatableBase *resolveTransformProperty(Transform &transform, const std::string &name) {
    if (name == "anchorPoint") {
        return &transform.anchorPoint;
    }
    if (name == "position") {
        return &transform.position;
    }
    if (name == "scale") {
        return &transform.scale;
    }
    if (name == "rotation") {
        return &transform.rotation;
    }
    if (name == "opacity") {
        return &transform.opacity;
    }
    return nullptr;
}

// Terminal properties on concrete shape types.
AnimatableBase *resolveShapeProperty(ShapeElement *element, const std::string &name) {
    switch (element->type()) {
        case ShapeType::Path: {
            if (name == "path") {
                return &static_cast<ShapePath *>(element)->path;
            }
            break;
        }
        case ShapeType::Rect: {
            if (name == "position") {
                return &static_cast<ShapeRect *>(element)->position;
            }
            if (name == "size") {
                return &static_cast<ShapeRect *>(element)->size;
            }
            if (name == "cornerRadius") {
                return &static_cast<ShapeRect *>(element)->cornerRadius;
            }
            break;
        }
        case ShapeType::Ellipse: {
            if (name == "position") {
                return &static_cast<ShapeEllipse *>(element)->position;
            }
            if (name == "size") {
                return &static_cast<ShapeEllipse *>(element)->size;
            }
            break;
        }
        case ShapeType::TrimPath: {
            if (name == "start") {
                return &static_cast<ShapeTrimPath *>(element)->start;
            }
            if (name == "end") {
                return &static_cast<ShapeTrimPath *>(element)->end;
            }
            if (name == "offset") {
                return &static_cast<ShapeTrimPath *>(element)->offset;
            }
            break;
        }
    }
    return nullptr;
}

AnimatableBase *resolveStyleProperty(LayerStyle *style, const std::string &name) {
    switch (style->type()) {
        case LayerStyleType::Fill: {
            auto *fill = static_cast<FillStyle *>(style);
            if (name == "color") {
                return &fill->color;
            }
            break;
        }
        case LayerStyleType::Stroke: {
            auto *stroke = static_cast<StrokeStyle *>(style);
            if (name == "color") {
                return &stroke->color;
            }
            if (name == "width") {
                return &stroke->width;
            }
            if (name == "trimStart") {
                return &stroke->trimStart;
            }
            if (name == "trimEnd") {
                return &stroke->trimEnd;
            }
            if (name == "trimOffset") {
                return &stroke->trimOffset;
            }
            break;
        }
    }
    return nullptr;
}

AnimatableBase *resolveShapeElementPath(ShapeElement *element,
                                        const std::vector<PathSegment> &segments) {
    if (segments.size() == 1) {
        return resolveShapeProperty(element, segments[0].name);
    }
    return nullptr;
}

}  // namespace

AnimatableBase *ResolveAnimatable(Document &document, const PropertyPath &property) {
    const std::vector<PathSegment> segments = ParsePropertyPath(property.path);
    if (segments.empty()) {
        return nullptr;
    }

    if (Layer *layer = document.entityIndex().findLayer(property.entityId)) {
        const PathSegment &first = segments[0];
        if (first.name == "transform" && segments.size() == 2) {
            return resolveTransformProperty(layer->transform, segments[1].name);
        }
        if (first.name == "followPath" && segments.size() == 2) {
            const std::string &name = segments[1].name;
            if (name == "pathOffset") {
                return &layer->followPath.pathOffset;
            }
            if (name == "orientOffset") {
                return &layer->followPath.orientOffset;
            }
            return nullptr;
        }
        if (first.name == "styles" && first.index >= 0 && segments.size() == 2) {
            if (first.index >= static_cast<int>(layer->styles.size())) {
                return nullptr;
            }
            return resolveStyleProperty(layer->styles[static_cast<size_t>(first.index)].get(),
                                        segments[1].name);
        }
        if (first.name == "masks" && first.index >= 0 && segments.size() == 2) {
            if (first.index >= static_cast<int>(layer->masks.size())) {
                return nullptr;
            }
            Mask &mask = layer->masks[static_cast<size_t>(first.index)];
            const std::string &name = segments[1].name;
            if (name == "path") {
                return &mask.path;
            }
            if (name == "opacity") {
                return &mask.opacity;
            }
            if (name == "feather") {
                return &mask.feather;
            }
            if (name == "expansion") {
                return &mask.expansion;
            }
            return nullptr;
        }
        if (segments.size() == 1 && layer->content->type() == LayerType::Shape) {
            auto *shapeContent = static_cast<ShapeContent *>(layer->content.get());
            if (!shapeContent->geometry) {
                return nullptr;
            }
            return resolveShapeProperty(shapeContent->geometry.get(), first.name);
        }
        if (first.name == "content" && segments.size() == 2) {
            if (layer->content->type() != LayerType::Text) {
                return nullptr;
            }
            auto *textContent = static_cast<TextContent *>(layer->content.get());
            if (segments[1].name == "text") {
                return &textContent->text;
            }
            if (segments[1].name == "fontSize") {
                return &textContent->fontSize;
            }
            return nullptr;
        }
        if (first.name == "image" && segments.size() == 2 &&
            layer->content->type() == LayerType::Image) {
            auto *imageContent = static_cast<ImageContent *>(layer->content.get());
            if (segments[1].name == "size") {
                return &imageContent->size;
            }
            return nullptr;
        }
        return nullptr;
    }

    if (ShapeElement *shape = document.entityIndex().findShape(property.entityId)) {
        return resolveShapeElementPath(shape, segments);
    }

    return nullptr;
}

}  // namespace motion

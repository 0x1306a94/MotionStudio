#include "MotionStudio/model/PropertyPath.h"

#include <cctype>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeStroke.h"
#include "MotionStudio/model/ShapeTrimPath.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

bool PropertyPath::operator==(const PropertyPath& other) const {
    return entityId == other.entityId && path == other.path;
}

bool PropertyPath::operator!=(const PropertyPath& other) const {
    return !(*this == other);
}

bool PathSegment::operator==(const PathSegment& other) const {
    return name == other.name && index == other.index;
}

std::vector<PathSegment> ParsePropertyPath(const std::string& path) {
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

AnimatableBase* resolveTransformProperty(Transform& transform, const std::string& name) {
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
AnimatableBase* resolveShapeProperty(ShapeElement* element, const std::string& name) {
    switch (element->type()) {
        case ShapeType::Path:
            if (name == "path") {
                return &static_cast<ShapePath*>(element)->path;
            }
            break;
        case ShapeType::Fill:
            if (name == "color") {
                return &static_cast<ShapeFill*>(element)->color;
            }
            if (name == "opacity") {
                return &static_cast<ShapeFill*>(element)->opacity;
            }
            break;
        case ShapeType::Stroke:
            if (name == "color") {
                return &static_cast<ShapeStroke*>(element)->color;
            }
            if (name == "width") {
                return &static_cast<ShapeStroke*>(element)->width;
            }
            if (name == "opacity") {
                return &static_cast<ShapeStroke*>(element)->opacity;
            }
            break;
        case ShapeType::Rect:
            if (name == "position") {
                return &static_cast<ShapeRect*>(element)->position;
            }
            if (name == "size") {
                return &static_cast<ShapeRect*>(element)->size;
            }
            if (name == "cornerRadius") {
                return &static_cast<ShapeRect*>(element)->cornerRadius;
            }
            break;
        case ShapeType::Ellipse:
            if (name == "position") {
                return &static_cast<ShapeEllipse*>(element)->position;
            }
            if (name == "size") {
                return &static_cast<ShapeEllipse*>(element)->size;
            }
            break;
        case ShapeType::TrimPath:
            if (name == "start") {
                return &static_cast<ShapeTrimPath*>(element)->start;
            }
            if (name == "end") {
                return &static_cast<ShapeTrimPath*>(element)->end;
            }
            if (name == "offset") {
                return &static_cast<ShapeTrimPath*>(element)->offset;
            }
            break;
        case ShapeType::Group:
            break;
    }
    return nullptr;
}

// Walk segments[from..] from a shape element downward; the last segment must be a property name.
AnimatableBase* resolveShapeSegments(ShapeElement* element,
                                     const std::vector<PathSegment>& segments,
                                     size_t from) {
    ShapeElement* current = element;
    for (size_t i = from; i < segments.size(); ++i) {
        const PathSegment& segment = segments[i];

        if (segment.name == "elements" && segment.index >= 0) {
            if (current->type() != ShapeType::Group) {
                return nullptr;
            }
            auto* group = static_cast<ShapeGroup*>(current);
            if (segment.index >= int(group->elements.size())) {
                return nullptr;
            }
            current = group->elements[size_t(segment.index)].get();
            continue;
        }
        if (segment.name == "transform") {
            if (current->type() != ShapeType::Group || i + 1 != segments.size() - 1) {
                return nullptr;
            }
            auto* group = static_cast<ShapeGroup*>(current);
            return resolveTransformProperty(group->transform, segments[i + 1].name);
        }
        if (i != segments.size() - 1) {
            return nullptr;
        }
        return resolveShapeProperty(current, segment.name);
    }
    return nullptr;
}

}  // namespace

AnimatableBase* ResolveAnimatable(Document& document, const PropertyPath& property) {
    const std::vector<PathSegment> segments = ParsePropertyPath(property.path);
    if (segments.empty()) {
        return nullptr;
    }

    if (Layer* layer = document.entityIndex().findLayer(property.entityId)) {
        const PathSegment& first = segments[0];
        if (first.name == "transform" && segments.size() == 2) {
            return resolveTransformProperty(layer->transform, segments[1].name);
        }
        if (first.name == "elements" && first.index >= 0) {
            if (layer->content->type() != LayerType::Shape) {
                return nullptr;
            }
            auto* shapeContent = static_cast<ShapeContent*>(layer->content.get());
            if (first.index >= int(shapeContent->elements.size())) {
                return nullptr;
            }
            return resolveShapeSegments(shapeContent->elements[size_t(first.index)].get(),
                                        segments, 1);
        }
        if (first.name == "content" && segments.size() == 2) {
            if (layer->content->type() != LayerType::Text) {
                return nullptr;
            }
            auto* textContent = static_cast<TextContent*>(layer->content.get());
            if (segments[1].name == "text") {
                return &textContent->text;
            }
            if (segments[1].name == "fontSize") {
                return &textContent->fontSize;
            }
            return nullptr;
        }
        return nullptr;
    }

    if (ShapeElement* shape = document.entityIndex().findShape(property.entityId)) {
        return resolveShapeSegments(shape, segments, 0);
    }

    return nullptr;
}

}  // namespace motion

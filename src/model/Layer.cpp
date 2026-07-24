#include "MotionStudio/model/Layer.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

namespace {

std::unique_ptr<LayerContent> MakeContent(LayerType type) {
    switch (type) {
        case LayerType::Shape: {
            return std::make_unique<ShapeContent>();
        }
        case LayerType::Image: {
            return std::make_unique<ImageContent>();
        }
        case LayerType::Text: {
            return std::make_unique<TextContent>();
        }
        case LayerType::Group: {
            return std::make_unique<NullContent>();
        }
        case LayerType::Precomp: {
            return std::make_unique<PrecompContent>();
        }
    }
    return std::make_unique<NullContent>();
}

}  // namespace

Layer::Layer(LayerType type)
    : content(MakeContent(type))
    , type_(type) {
}

Layer::~Layer() = default;

LayerType Layer::type() const {
    return type_;
}

bool Layer::setParent(EntityId newParentId, const Document &document) {
    if (!newParentId.isValid()) {
        parentId = newParentId;
        return true;
    }
    // Walk up the target parent chain; reject if it would create a cycle.
    EntityId cursor = newParentId;
    while (cursor.isValid()) {
        if (cursor == id) {
            return false;
        }
        const Layer *ancestor = document.entityIndex().findLayer(cursor);
        if (!ancestor) {
            return false;  // Dangling parent chain, reject.
        }
        cursor = ancestor->parentId;
    }
    parentId = newParentId;
    return true;
}

Mat3 Layer::localTransform(FrameTime time) const {
    return Mat3::Translate(transform.position.evaluate(time)) *
        Mat3::Rotate(transform.rotation.evaluate(time)) *
        Mat3::Scale(transform.scale.evaluate(time)) *
        Mat3::Translate(-transform.anchorPoint.evaluate(time));
}

Mat3 Layer::worldTransform(FrameTime time, const Document &document) const {
    return worldTransform(time, document, 0);
}

Mat3 Layer::worldTransform(FrameTime time, const Document &document, int depth) const {
    Mat3 local = localTransform(time);
    // Depth guard: setParent prevents cycles at runtime; this catches cycles from deserialization.
    if (!parentId.isValid() || depth >= 1024) {
        return local;
    }
    const Layer *parent = document.entityIndex().findLayer(parentId);
    if (!parent) {
        return local;
    }
    return parent->worldTransform(time, document, depth + 1) * local;
}

}  // namespace motion

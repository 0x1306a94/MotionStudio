#include "MotionStudio/undo/ConvertGeometryToPathCommand.h"

#include <algorithm>
#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

BezierPath BakePathAtFrame(const ShapeElement &element, FrameTime frame) {
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            return shape.path.evaluate(frame);
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            const Vec2 center = shape.position.evaluate(frame);
            const Vec2 size = shape.size.evaluate(frame);
            const float halfWidth = std::max(size.x * 0.5f, 0.0f);
            const float halfHeight = std::max(size.y * 0.5f, 0.0f);
            const float cornerRadius = std::clamp(shape.cornerRadius.evaluate(frame), 0.0f,
                                                  std::min(halfWidth, halfHeight));
            return ShapeGeometryToBezierPath(MakeRectGeometry(center, size, cornerRadius));
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            return ShapeGeometryToBezierPath(
                MakeEllipseGeometry(shape.position.evaluate(frame), shape.size.evaluate(frame)));
        }
        case ShapeType::TrimPath: {
            return {};
        }
    }
    return {};
}

ShapeContent *ShapeContentOf(Document &document, EntityId layerId) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || layer->content == nullptr ||
        layer->content->type() != LayerType::Shape) {
        return nullptr;
    }
    return static_cast<ShapeContent *>(layer->content.get());
}

void SwapGeometry(Document &document, ShapeContent &content,
                  std::unique_ptr<ShapeElement> &stashed) {
    std::unique_ptr<ShapeElement> current = std::move(content.geometry);
    content.geometry = std::move(stashed);
    stashed = std::move(current);
    document.refreshEntityIndex();
}

}  // namespace

ConvertGeometryToPathCommand::ConvertGeometryToPathCommand(EntityId layerId, FrameTime frame)
    : layerId_(layerId)
    , frame_(frame) {
}

void ConvertGeometryToPathCommand::execute(Document &document) {
    ShapeContent *content = ShapeContentOf(document, layerId_);
    if (content == nullptr || content->geometry == nullptr) {
        return;
    }

    if (didConvert_) {
        if (!stashed_) {
            return;
        }
        SwapGeometry(document, *content, stashed_);
        return;
    }

    const ShapeType type = content->geometry->type();
    if (type != ShapeType::Rect && type != ShapeType::Ellipse) {
        return;
    }

    BezierPath baked = BakePathAtFrame(*content->geometry, frame_);
    if (baked.vertices.empty()) {
        return;
    }

    auto pathShape = std::make_unique<ShapePath>();
    pathShape->id = content->geometry->id;
    pathShape->path.setStaticValue(std::move(baked));

    stashed_ = std::move(content->geometry);
    content->geometry = std::move(pathShape);
    document.refreshEntityIndex();
    didConvert_ = true;
}

void ConvertGeometryToPathCommand::undo(Document &document) {
    if (!didConvert_ || !stashed_) {
        return;
    }
    ShapeContent *content = ShapeContentOf(document, layerId_);
    if (content == nullptr) {
        return;
    }
    SwapGeometry(document, *content, stashed_);
}

CommandKind ConvertGeometryToPathCommand::kind() const {
    return CommandKind::ConvertGeometryToPath;
}

std::string ConvertGeometryToPathCommand::describe() const {
    return "Convert to Path";
}

}  // namespace motion

// 验收：1000 次随机命令 + 随机 undo/redo，ASan 下无崩溃无泄漏；
// undo 后序列化指纹与操作前一致。固定种子，可复现。
#include <memory>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/Commands.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::AddKeyframeCommand;
using motion::AddLayerCommand;
using motion::Color;
using motion::Command;
using motion::Composition;
using motion::documentFingerprint;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::MoveKeyframeCommand;
using motion::MoveLayerCommand;
using motion::PropertyPath;
using motion::PropertyValue;
using motion::RemoveKeyframeCommand;
using motion::RemoveLayerCommand;
using motion::Serializer;
using motion::SetEasingCommand;
using motion::SetStaticValueCommand;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::UndoManager;
using motion::Vec2;

namespace {

const char* kTransformProperties[] = {"transform.position", "transform.scale",
                                      "transform.anchorPoint"};

Document makeRandomDocument(std::mt19937_64& rng) {
    Document document;
    document.name = "fuzz";
    const int compositionCount = 1 + int(rng() % 2);
    for (int i = 0; i < compositionCount; ++i) {
        auto composition = std::make_unique<Composition>();
        composition->duration = 100;
        const int layerCount = int(rng() % 4);
        for (int j = 0; j < layerCount; ++j) {
            auto layer = std::make_unique<Layer>(LayerType::Shape);
            auto* shapeContent = static_cast<ShapeContent*>(layer->content.get());
            auto fill = std::make_unique<ShapeFill>();
            shapeContent->elements.push_back(std::move(fill));
            composition->layers.push_back(std::move(layer));
        }
        document.addComposition(std::move(composition));
    }
    return document;
}

std::vector<Layer*> collectLayers(Document& document) {
    std::vector<Layer*> layers;
    for (auto& composition : document.compositions) {
        for (auto& layer : composition->layers) {
            layers.push_back(layer.get());
        }
    }
    return layers;
}

float randomFloat(std::mt19937_64& rng) { return float(rng() % 1000) - 500.0f; }

Vec2 randomVec2(std::mt19937_64& rng) { return {randomFloat(rng), randomFloat(rng)}; }

// 生成一条随机命令（目标可能不存在——命令必须安全跳过）。
std::unique_ptr<Command> makeRandomCommand(std::mt19937_64& rng, Document& document) {
    std::vector<Layer*> layers = collectLayers(document);
    EntityId compositionId =
        document.compositions.empty() ? EntityId{rng() % 100}
                                      : document.compositions[rng() %
                                                              document.compositions.size()]->id;
    EntityId layerId = layers.empty() ? EntityId{rng() % 100}
                                      : layers[rng() % layers.size()]->id;
    PropertyPath property{layerId, kTransformProperties[rng() % 3]};
    const FrameTime time = FrameTime(rng() % 100);

    switch (rng() % 7) {
        case 0:
            return std::make_unique<AddLayerCommand>(
                compositionId, std::make_unique<Layer>(LayerType::Null));
        case 1:
            return std::make_unique<RemoveLayerCommand>(compositionId, layerId);
        case 2:
            return std::make_unique<MoveLayerCommand>(compositionId, int(rng() % 6),
                                                      int(rng() % 6));
        case 3:
            return std::make_unique<SetStaticValueCommand>(
                property, PropertyValue{randomVec2(rng)});
        case 4: {
            Keyframe<Vec2> keyframe;
            keyframe.time = time;
            keyframe.value = randomVec2(rng);
            keyframe.easing = rng() % 2 ? Easing::EaseOut() : Easing::Linear();
            return std::make_unique<AddKeyframeCommand>(
                property, motion::KeyframeData{keyframe});
        }
        case 5:
            return rng() % 2
                       ? std::unique_ptr<Command>(
                             std::make_unique<RemoveKeyframeCommand>(property, time))
                       : std::make_unique<MoveKeyframeCommand>(
                             property, time, FrameTime(rng() % 100));
        default: {
            const Easing easings[] = {Easing::Linear(), Easing::EaseIn(),
                                      Easing::EaseOut(), Easing::Hold()};
            return std::make_unique<SetEasingCommand>(property, time,
                                                      easings[rng() % 4]);
        }
    }
}

}  // namespace

TEST(RandomOpsTest, ThousandRandomOpsNoCrashAndRoundTripStable) {
    std::mt19937_64 rng(20260720);
    Document document = makeRandomDocument(rng);
    UndoManager undo;

    for (int iteration = 0; iteration < 1000; ++iteration) {
        const uint64_t choice = rng() % 10;
        if (choice <= 6) {
            undo.execute(document, makeRandomCommand(rng, document));
        } else if (choice <= 8 && undo.canUndo()) {
            undo.undo(document);
        } else if (undo.canRedo()) {
            undo.redo(document);
        }
    }

    const std::string json = Serializer::serialize(document);
    auto restored = Serializer::deserialize(json);
    EXPECT_EQ(Serializer::serialize(*restored), json);
}

TEST(RandomOpsTest, UndoRestoresSerializationFingerprint) {
    std::mt19937_64 rng(42);
    Document document = makeRandomDocument(rng);
    UndoManager undo;

    for (int iteration = 0; iteration < 200; ++iteration) {
        const uint64_t before = documentFingerprint(document);
        undo.execute(document, makeRandomCommand(rng, document));
        undo.undo(document);
        EXPECT_EQ(documentFingerprint(document), before) << "iteration " << iteration;
    }
}

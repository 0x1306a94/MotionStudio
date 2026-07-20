#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"

using motion::Composition;
using motion::Document;
using motion::Layer;
using motion::LayerType;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::ShapeGroup;

namespace {

Composition* addComposition(Document& document, const char* name) {
    auto composition = std::make_unique<Composition>();
    composition->name = name;
    return document.addComposition(std::move(composition));
}

Layer* addShapeLayer(Document& document, motion::EntityId compositionId) {
    return document.addLayer(compositionId, std::make_unique<Layer>(LayerType::Shape));
}

}  // namespace

TEST(DocumentTest, AddCompositionRegistersInIndex) {
    Document document;
    Composition* composition = addComposition(document, "main");

    ASSERT_NE(composition, nullptr);
    EXPECT_EQ(document.entityIndex().findComposition(composition->id), composition);
}

TEST(DocumentTest, AddLayerRegistersInIndex) {
    Document document;
    Composition* composition = addComposition(document, "main");
    Layer* layer = addShapeLayer(document, composition->id);

    EXPECT_EQ(document.entityIndex().findLayer(layer->id), layer);
    ASSERT_EQ(composition->layers.size(), 1u);
}

TEST(DocumentTest, AddLayerAtIndexInserts) {
    Document document;
    Composition* composition = addComposition(document, "main");
    Layer* bottom = addShapeLayer(document, composition->id);
    Layer* top = addShapeLayer(document, composition->id);
    Layer* middle = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Null), 1);

    ASSERT_EQ(composition->layers.size(), 3u);
    EXPECT_EQ(composition->layers[0]->id, bottom->id);
    EXPECT_EQ(composition->layers[1]->id, middle->id);
    EXPECT_EQ(composition->layers[2]->id, top->id);
}

TEST(DocumentTest, AddLayerToMissingCompositionReturnsNull) {
    Document document;
    Layer* layer = document.addLayer(motion::EntityId{123}, std::make_unique<Layer>(LayerType::Shape));
    EXPECT_EQ(layer, nullptr);
}

TEST(DocumentTest, TakeLayerUnregistersSubtree) {
    Document document;
    Composition* composition = addComposition(document, "main");
    Layer* layer = addShapeLayer(document, composition->id);

    auto* shapeContent = static_cast<ShapeContent*>(layer->content.get());
    auto group = std::make_unique<ShapeGroup>();
    auto nestedFill = std::make_unique<ShapeFill>();
    const motion::EntityId nestedId = nestedFill->id;
    group->elements.push_back(std::move(nestedFill));
    shapeContent->elements.push_back(std::move(group));
    document.refreshEntityIndex();
    ASSERT_NE(document.entityIndex().findShape(nestedId), nullptr);

    std::unique_ptr<Layer> taken = document.takeLayer(composition->id, layer->id);
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(taken->id, layer->id);
    EXPECT_EQ(document.entityIndex().findLayer(layer->id), nullptr);
    EXPECT_EQ(document.entityIndex().findShape(nestedId), nullptr);
    EXPECT_TRUE(composition->layers.empty());
}

TEST(DocumentTest, TakeCompositionRemovesWholeTree) {
    Document document;
    Composition* composition = addComposition(document, "main");
    Layer* layer = addShapeLayer(document, composition->id);

    std::unique_ptr<Composition> taken = document.takeComposition(composition->id);
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(document.entityIndex().findComposition(composition->id), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(layer->id), nullptr);
    EXPECT_TRUE(document.compositions.empty());
}

TEST(DocumentTest, MoveLayerReorders) {
    Document document;
    Composition* composition = addComposition(document, "main");
    Layer* first = addShapeLayer(document, composition->id);
    Layer* second = addShapeLayer(document, composition->id);
    Layer* third = addShapeLayer(document, composition->id);

    ASSERT_TRUE(document.moveLayer(composition->id, 0, 2));
    EXPECT_EQ(composition->layers[0]->id, second->id);
    EXPECT_EQ(composition->layers[1]->id, third->id);
    EXPECT_EQ(composition->layers[2]->id, first->id);

    EXPECT_FALSE(document.moveLayer(composition->id, 0, 5));
    EXPECT_FALSE(document.moveLayer(composition->id, -1, 0));
}

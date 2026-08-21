#include <gtest/gtest.h>
#include <memory>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/GeometryRevision.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/common/VectorNetworkEdit.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::BezierPath;
using motion::VectorNetwork;

TEST(GeometryRevisionTest, DefaultIsZeroAndStampIsNonZeroAndIncreases) {
    VectorNetwork network;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(network), 0u);
    motion::GeometryRevisionAccess::Stamp(network);
    const uint64_t first = motion::GeometryRevisionAccess::Get(network);
    EXPECT_NE(first, 0u);
    motion::GeometryRevisionAccess::Stamp(network);
    EXPECT_GT(motion::GeometryRevisionAccess::Get(network), first);

    BezierPath path;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path), 0u);
    motion::GeometryRevisionAccess::Stamp(path);
    const uint64_t pathFirst = motion::GeometryRevisionAccess::Get(path);
    EXPECT_NE(pathFirst, 0u);
    motion::GeometryRevisionAccess::Stamp(path);
    EXPECT_GT(motion::GeometryRevisionAccess::Get(path), pathFirst);
}

TEST(GeometryRevisionTest, CopyKeepsRevisionUntilRestamped) {
    VectorNetwork network;
    network.vertices.push_back({1, {0, 0}});
    motion::GeometryRevisionAccess::Stamp(network);
    const uint64_t stamped = motion::GeometryRevisionAccess::Get(network);
    const VectorNetwork copied = network;
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(copied), stamped);
    network.vertices[0].point = {1, 0};
    motion::GeometryRevisionAccess::Stamp(network);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(network), stamped);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(copied), stamped);
}

TEST(GeometryRevisionTest, EqualityIgnoresRevision) {
    VectorNetwork left;
    left.vertices.push_back({1, {0, 0}});
    VectorNetwork right = left;
    motion::GeometryRevisionAccess::Stamp(left);
    motion::GeometryRevisionAccess::Stamp(right);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(left), motion::GeometryRevisionAccess::Get(right));
    EXPECT_EQ(left, right);

    BezierPath pathLeft;
    pathLeft.contours.push_back({{{0, 0}, {}, {}}, false});
    BezierPath pathRight = pathLeft;
    motion::GeometryRevisionAccess::Stamp(pathLeft);
    motion::GeometryRevisionAccess::Stamp(pathRight);
    EXPECT_EQ(pathLeft, pathRight);
}

TEST(GeometryRevisionTest, MakeSingleContourStamps) {
    const BezierPath path = motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(path), 0u);
}

TEST(GeometryRevisionTest, BezierPathToVectorNetworkStamps) {
    const BezierPath path =
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    const VectorNetwork network = motion::BezierPathToVectorNetwork(path);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(network), 0u);
}

TEST(GeometryRevisionTest, MoveVertexStampsAndMissingVertexDoesNot) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const uint64_t before = motion::GeometryRevisionAccess::Get(network);
    ASSERT_NE(before, 0u);
    const VectorNetwork missing = motion::MoveVertex(network, 999, {1, 1});
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(missing), before);
    const VectorNetwork moved = motion::MoveVertex(network, network.vertices[0].id, {1, 1});
    EXPECT_NE(motion::GeometryRevisionAccess::Get(moved), before);
}

TEST(GeometryRevisionTest, EvaluatePreviewKeepsStaticRevision) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    motion::Animatable<VectorNetwork> path;
    path.setStaticValue(network);
    const uint64_t stored = motion::GeometryRevisionAccess::Get(path.staticValue());
    EXPECT_NE(stored, 0u);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluatePreview(0)), stored);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluatePreview(10)), stored);
}

TEST(GeometryRevisionTest, HoldKeepsKeyframeRevision) {
    VectorNetwork a = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork b = a;
    b.vertices[1].point = {20, 0};
    motion::GeometryRevisionAccess::Stamp(b);
    motion::Animatable<VectorNetwork> path;
    motion::Keyframe<VectorNetwork> kf0;
    kf0.time = 0;
    kf0.value = a;
    kf0.easing = motion::Easing::Hold();
    motion::Keyframe<VectorNetwork> kf1;
    kf1.time = 10;
    kf1.value = b;
    path.addKeyframe(kf0);
    path.addKeyframe(kf1);
    const uint64_t fromRevision = motion::GeometryRevisionAccess::Get(path.keyframes()[0].value);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluate(0)), fromRevision);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(path.evaluate(5)), fromRevision);
}

TEST(GeometryRevisionTest, LerpSameTopologyGetsNewRevision) {
    VectorNetwork from = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork to = from;
    to.vertices[1].point = {20, 0};
    motion::GeometryRevisionAccess::Stamp(to);
    const VectorNetwork lerped = motion::Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(lerped), motion::GeometryRevisionAccess::Get(from));
    EXPECT_NE(motion::GeometryRevisionAccess::Get(lerped), motion::GeometryRevisionAccess::Get(to));
}

TEST(GeometryRevisionTest, LerpDifferentTopologyKeepsFromRevision) {
    VectorNetwork from = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false));
    VectorNetwork to = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const VectorNetwork held = motion::Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(held), motion::GeometryRevisionAccess::Get(from));
}

TEST(GeometryRevisionTest, JsonRoundTripOmitsRevisionAndRestamps) {
    motion::Document document;
    motion::Composition *composition = document.addComposition(std::make_unique<motion::Composition>());
    motion::Layer *layer =
        document.addLayer(composition->id, std::make_unique<motion::Layer>(motion::LayerType::Shape));
    auto *content = static_cast<motion::ShapeContent *>(layer->content.get());
    auto shape = std::make_unique<motion::ShapePath>();
    shape->path.setStaticValue(motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true)));
    content->geometry = std::move(shape);
    const std::string json = motion::Serializer::serialize(document);
    EXPECT_EQ(json.find("\"revision\""), std::string::npos);
    auto first = motion::Serializer::deserialize(json);
    auto second = motion::Serializer::deserialize(json);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    const auto *a = static_cast<const motion::ShapePath *>(
        static_cast<const motion::ShapeContent *>((*first)->compositions[0]->layers[0]->content.get())
            ->geometry.get());
    const auto *b = static_cast<const motion::ShapePath *>(
        static_cast<const motion::ShapeContent *>((*second)->compositions[0]->layers[0]->content.get())
            ->geometry.get());
    const uint64_t revA = motion::GeometryRevisionAccess::Get(a->path.staticValue());
    const uint64_t revB = motion::GeometryRevisionAccess::Get(b->path.staticValue());
    EXPECT_NE(revA, 0u);
    EXPECT_NE(revB, 0u);
    EXPECT_NE(revA, revB);
    EXPECT_EQ(a->path.staticValue(), b->path.staticValue());
}

TEST(GeometryRevisionTest, StampedCompileHitsKeepFillRevision) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const BezierPath first = motion::CompileFillFaces(network);
    const BezierPath second = motion::CompileFillFaces(network);
    EXPECT_EQ(first, second);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(first), 0u);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(first), motion::GeometryRevisionAccess::Get(second));
}

TEST(GeometryRevisionTest, UnstampedCompileDoesNotShareFillRevision) {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {0, 10}}};
    network.edges = {{1, 1, 2, {}, {}}, {2, 2, 3, {}, {}}, {3, 3, 1, {}, {}}};
    ASSERT_EQ(motion::GeometryRevisionAccess::Get(network), 0u);
    const BezierPath first = motion::CompileFillFaces(network);
    const BezierPath second = motion::CompileFillFaces(network);
    EXPECT_EQ(first, second);
    EXPECT_NE(motion::GeometryRevisionAccess::Get(first), motion::GeometryRevisionAccess::Get(second));
}

TEST(GeometryRevisionTest, CompileVectorNetworkSharesLookup) {
    VectorNetwork network = motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
    const motion::CompiledVectorNetwork compiled = motion::CompileVectorNetwork(network);
    EXPECT_EQ(compiled.fill, motion::CompileFillFaces(network));
    EXPECT_EQ(compiled.stroke, motion::CompileStrokeEdges(network));
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(compiled.fill),
              motion::GeometryRevisionAccess::Get(motion::CompileFillFaces(network)));
}

TEST(GeometryRevisionTest, StaticShapePathKeepsCompiledRevisionAcrossFrames) {
    motion::Document document;
    motion::Composition *composition = document.addComposition(std::make_unique<motion::Composition>());
    composition->duration = 100;
    motion::Layer *layer =
        document.addLayer(composition->id, std::make_unique<motion::Layer>(motion::LayerType::Shape));
    layer->outPoint = 100;
    auto *content = static_cast<motion::ShapeContent *>(layer->content.get());
    auto shape = std::make_unique<motion::ShapePath>();
    shape->path.setStaticValue(motion::BezierPathToVectorNetwork(
        motion::MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true)));
    content->geometry = std::move(shape);
    auto fill = std::make_unique<motion::FillStyle>();
    fill->color.setStaticValue(motion::Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    auto frame0 = motion::SceneEvaluator::Evaluate(document, composition->id, 0);
    auto frame1 = motion::SceneEvaluator::Evaluate(document, composition->id, 1);
    ASSERT_TRUE(frame0.hasValue());
    ASSERT_TRUE(frame1.hasValue());
    ASSERT_FALSE(frame0->layers.empty());
    ASSERT_FALSE(frame0->layers[0].shapeItems.empty());
    const uint64_t rev0 =
        motion::GeometryRevisionAccess::Get(frame0->layers[0].shapeItems[0].geometry.path);
    const uint64_t rev1 =
        motion::GeometryRevisionAccess::Get(frame1->layers[0].shapeItems[0].geometry.path);
    EXPECT_NE(rev0, 0u);
    EXPECT_EQ(rev0, rev1);
    EXPECT_EQ(frame0->layers[0].shapeNetwork, frame1->layers[0].shapeNetwork);
    EXPECT_EQ(motion::GeometryRevisionAccess::Get(frame0->layers[0].shapeNetwork),
              motion::GeometryRevisionAccess::Get(frame1->layers[0].shapeNetwork));
}

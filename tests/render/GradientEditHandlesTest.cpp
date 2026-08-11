#include <gtest/gtest.h>

#include <cmath>

#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/GradientEditHandles.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BuildGradientEditCommands;
using motion::BuildGradientEditHandles;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::GradientEditHandles;
using motion::GradientEditTarget;
using motion::GradientHandleKind;
using motion::GradientType;
using motion::HitTestGradientEdit;
using motion::MakeRectGeometry;
using motion::Mat3;
using motion::SceneState;
using motion::StylePaintMode;
using motion::Vec2;

namespace {

EvaluatedShapeItem MakeGradientItem(GradientType type, Vec2 start, Vec2 end, int styleIndex,
                                    float startAngle = 0.f, float endAngle = 360.f) {
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry({0, 0}, {100, 100});
    item.paint.paintMode = StylePaintMode::Gradient;
    item.paint.gradient.type = type;
    item.paint.gradient.start = start;
    item.paint.gradient.end = end;
    item.paint.gradient.startAngle = startAngle;
    item.paint.gradient.endAngle = endAngle;
    item.paint.gradient.stops = {{{0, 0, 0, 1}, 0.f}, {{1, 1, 1, 1}, 1.f}};
    item.styleIndex = styleIndex;
    return item;
}

SceneState MakeStateWithGradient(GradientType type, Vec2 start, Vec2 end, Mat3 world,
                                 int styleIndex = 0, float startAngle = 0.f,
                                 float endAngle = 360.f) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{1};
    layer.worldTransform = world;
    layer.shapeItems.push_back(
        MakeGradientItem(type, start, end, styleIndex, startAngle, endAngle));
    state.layers.push_back(std::move(layer));
    return state;
}

}  // namespace

TEST(GradientEditHandlesTest, MissingTargetIsInvalid) {
    SceneState state;
    GradientEditHandles handles;
    GradientEditTarget target;
    target.layerId = EntityId{1};
    target.styleIndex = 0;
    EXPECT_FALSE(BuildGradientEditHandles(state, target, handles));
    EXPECT_FALSE(handles.valid);
    EXPECT_TRUE(BuildGradientEditCommands(handles, 1.5f, 7.0f).empty());
}

TEST(GradientEditHandlesTest, BuildLinearWorldPoints) {
    auto state = MakeStateWithGradient(GradientType::Linear, {10, 20}, {110, 20},
                                       Mat3::Translate({5, 7}));
    GradientEditTarget target{EntityId{1}, 0};
    GradientEditHandles handles;
    ASSERT_TRUE(BuildGradientEditHandles(state, target, handles));
    EXPECT_TRUE(handles.valid);
    EXPECT_EQ(handles.type, GradientType::Linear);
    EXPECT_FLOAT_EQ(handles.worldStart.x, 15);
    EXPECT_FLOAT_EQ(handles.worldStart.y, 27);
    EXPECT_FLOAT_EQ(handles.worldEnd.x, 115);
    EXPECT_FLOAT_EQ(handles.worldEnd.y, 27);
}

TEST(GradientEditHandlesTest, HitTestPrefersEndOverStartWhenCloser) {
    auto state =
        MakeStateWithGradient(GradientType::Linear, {0, 0}, {100, 0}, Mat3::Identity());
    GradientEditHandles handles;
    ASSERT_TRUE(BuildGradientEditHandles(state, {EntityId{1}, 0}, handles));

    EXPECT_EQ(HitTestGradientEdit(handles, {100, 0}, 8.0f), GradientHandleKind::End);
    EXPECT_EQ(HitTestGradientEdit(handles, {0, 0}, 8.0f), GradientHandleKind::Start);
    EXPECT_EQ(HitTestGradientEdit(handles, {50, 50}, 8.0f), GradientHandleKind::None);
}

TEST(GradientEditHandlesTest, ConicHitTestsAngleHandles) {
    auto state = MakeStateWithGradient(GradientType::Conic, {50, 50}, {150, 50}, Mat3::Identity(),
                                       0, 0.f, 90.f);
    GradientEditHandles handles;
    ASSERT_TRUE(BuildGradientEditHandles(state, {EntityId{1}, 0}, handles));
    EXPECT_EQ(handles.type, GradientType::Conic);

    const float radius = 100.f;
    const Vec2 startAnglePoint{50.f + radius, 50.f};
    const Vec2 endAnglePoint{50.f, 50.f + radius};
    EXPECT_EQ(HitTestGradientEdit(handles, startAnglePoint, 8.0f),
              GradientHandleKind::StartAngle);
    EXPECT_EQ(HitTestGradientEdit(handles, endAnglePoint, 8.0f), GradientHandleKind::EndAngle);
    EXPECT_EQ(HitTestGradientEdit(handles, {50, 50}, 8.0f), GradientHandleKind::Start);
}

TEST(GradientEditHandlesTest, BuildCommandsIncludeStrokeAndHandles) {
    auto state =
        MakeStateWithGradient(GradientType::Linear, {0, 0}, {80, 0}, Mat3::Identity());
    GradientEditHandles handles;
    ASSERT_TRUE(BuildGradientEditHandles(state, {EntityId{1}, 0}, handles));
    const auto commands = BuildGradientEditCommands(handles, 1.5f, 7.0f);
    ASSERT_FALSE(commands.empty());
    bool hasStroke = false;
    bool hasFill = false;
    for (const auto &command : commands) {
        hasStroke = hasStroke || command.type == DrawCommandType::StrokePath;
        hasFill = hasFill || command.type == DrawCommandType::DrawPath;
    }
    EXPECT_TRUE(hasStroke);
    EXPECT_TRUE(hasFill);
}

TEST(GradientEditHandlesTest, StyleIndexMustMatch) {
    auto state =
        MakeStateWithGradient(GradientType::Radial, {0, 0}, {40, 0}, Mat3::Identity(), 2);
    GradientEditHandles handles;
    EXPECT_FALSE(BuildGradientEditHandles(state, {EntityId{1}, 0}, handles));
    ASSERT_TRUE(BuildGradientEditHandles(state, {EntityId{1}, 2}, handles));
    EXPECT_EQ(handles.type, GradientType::Radial);
}

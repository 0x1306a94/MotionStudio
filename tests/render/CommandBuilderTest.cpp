#include <gtest/gtest.h>

#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/EvaluatedImageItem.h"
#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/MaskApplyMode.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BlendMode;
using motion::BuildCommands;
using motion::BuildSelectionOutlineCommands;
using motion::Color;
using motion::DrawCommand;
using motion::DrawCommandType;
using motion::DropShadowStyle;
using motion::EntityId;
using motion::EvaluatedImageItem;
using motion::EvaluatedLayer;
using motion::EvaluatedMask;
using motion::EvaluatedShapeItem;
using motion::EvaluatedTextItem;
using motion::GaussianBlurEffect;
using motion::LayerEffectType;
using motion::LayerFxType;
using motion::LineCap;
using motion::LineJoin;
using motion::MakePathGeometry;
using motion::MakeSingleContour;
using motion::MaskApplyMode;
using motion::MaskMode;
using motion::Mat3;
using motion::Paint;
using motion::SceneState;
using motion::ShapeGeometryKind;
using motion::TrackMatteType;
using motion::Vec2;

namespace {

EvaluatedShapeItem MakeFillItem(BlendMode blendMode = BlendMode::Normal) {
    EvaluatedShapeItem item;
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, true);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{1, 0, 0, 1}, motion::FillRule::NonZero, blendMode};
    return item;
}

EvaluatedShapeItem MakeStrokeItem(BlendMode blendMode = BlendMode::Normal) {
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 10}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}, motion::FillRule::NonZero, blendMode};
    item.stroke.width = 3;
    item.stroke.cap = LineCap::Round;
    return item;
}

}  // namespace

TEST(CommandBuilderTest, EmptySceneProducesNoCommands) {
    SceneState state;
    EXPECT_TRUE(BuildCommands(state).empty());
}

TEST(CommandBuilderTest, LayerExpandsToScopedDrawSequence) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 0.5f;
    layer.blendMode = BlendMode::Screen;
    layer.shapeItems.push_back(MakeFillItem(BlendMode::Screen));
    layer.shapeItems.push_back(MakeStrokeItem(BlendMode::Screen));
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 7u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[1].type, DrawCommandType::ConcatTransform);
    EXPECT_EQ(commands[2].type, DrawCommandType::SetOpacity);
    EXPECT_FLOAT_EQ(commands[2].opacity, 0.5f);
    EXPECT_EQ(commands[3].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[3].blendMode, BlendMode::Screen);
    EXPECT_EQ(commands[4].type, DrawCommandType::DrawPath);
    EXPECT_EQ(commands[4].paint.color, (Color{1, 0, 0, 1}));
    EXPECT_EQ(commands[5].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[6].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, ItemBlendModeEmitsSetBlendModeOnChange) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem(BlendMode::Add));
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    // Layer-level SetBlendMode, then a blend switch before each diverging item.
    ASSERT_EQ(commands.size(), 9u);
    EXPECT_EQ(commands[3].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[3].blendMode, BlendMode::Normal);
    EXPECT_EQ(commands[4].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[4].blendMode, BlendMode::Add);
    EXPECT_EQ(commands[5].type, DrawCommandType::DrawPath);
    EXPECT_EQ(commands[6].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[6].blendMode, BlendMode::Normal);
    EXPECT_EQ(commands[7].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[8].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, StrokeItemCarriesStrokeParameters) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 6u);
    EXPECT_EQ(commands[4].type, DrawCommandType::StrokePath);
    EXPECT_FLOAT_EQ(commands[4].stroke.width, 3.0f);
    EXPECT_EQ(commands[4].stroke.cap, LineCap::Round);
    EXPECT_EQ(commands[4].paint.color, (Color{0, 0, 1, 1}));
}

TEST(CommandBuilderTest, MultipleLayersKeepRenderOrder) {
    SceneState state;
    EvaluatedLayer bottom;
    bottom.shapeItems.push_back(MakeFillItem());
    EvaluatedLayer top;
    top.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(bottom));
    state.layers.push_back(std::move(top));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 12u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[5].type, DrawCommandType::Restore);
    EXPECT_EQ(commands[6].type, DrawCommandType::Save);
    EXPECT_EQ(commands[11].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, EmptyGroupEmitsNoDrawCommands) {
    SceneState state;
    EvaluatedLayer group;
    group.id = EntityId{1};
    state.layers.push_back(group);

    EXPECT_TRUE(BuildCommands(state).empty());
}

TEST(CommandBuilderTest, SelectionOutlineBuildsStrokeForSelectedLayerBounds) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildSelectionOutlineCommands(state, {EntityId{42}}, EntityId{42}, 1.5f, 7.0f);

    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands[0].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[0].paint.color, (Color{0.0f, 0.47843137f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(commands[0].stroke.width, 1.5f);
    EXPECT_EQ(commands[0].geometry.kind, ShapeGeometryKind::Path);
}

TEST(CommandBuilderTest, SelectionOutlineSkipsMissingLayers) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));

    EXPECT_TRUE(BuildSelectionOutlineCommands(state, {EntityId{7}}, EntityId{7}, 1.5f, 7.0f).empty());
}

TEST(CommandBuilderTest, PathMasksEmitBeginLayerAndDrawMaskPath) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    EvaluatedMask mask;
    mask.mode = MaskMode::Subtract;
    mask.opacity = 0.75f;
    mask.inverted = true;
    mask.feather = 4.0f;
    mask.expansion = 1.0f;
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{5, 0}, {}, {}}}, true);
    mask.path = path;
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_GE(commands.size(), 10u);
    EXPECT_EQ(commands[4].type, DrawCommandType::BeginLayer);
    EXPECT_EQ(commands[5].type, DrawCommandType::DrawPath);

    bool sawBeginMask = false;
    bool sawDrawMask = false;
    bool sawEndMask = false;
    bool sawEndLayer = false;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginMask) {
            sawBeginMask = true;
            EXPECT_EQ(command.maskApplyMode, MaskApplyMode::PathCoverage);
        }
        if (command.type == DrawCommandType::DrawMaskPath) {
            sawDrawMask = true;
            EXPECT_EQ(command.maskMode, MaskMode::Subtract);
            EXPECT_FLOAT_EQ(command.maskOpacity, 0.75f);
            EXPECT_TRUE(command.maskInverted);
            EXPECT_FLOAT_EQ(command.maskFeather, 4.0f);
            EXPECT_FLOAT_EQ(command.maskExpansion, 1.0f);
        }
        if (command.type == DrawCommandType::EndMask) {
            sawEndMask = true;
        }
        if (command.type == DrawCommandType::EndLayer) {
            sawEndLayer = true;
        }
    }
    EXPECT_TRUE(sawBeginMask);
    EXPECT_TRUE(sawDrawMask);
    EXPECT_TRUE(sawEndMask);
    EXPECT_TRUE(sawEndLayer);
}

TEST(CommandBuilderTest, SkipsUsedAsMatteOnlyLayers) {
    SceneState state;
    EvaluatedLayer source;
    source.id = EntityId{1};
    source.usedAsMatteOnly = true;
    source.shapeItems.push_back(MakeFillItem());
    EvaluatedLayer target;
    target.id = EntityId{2};
    target.shapeItems.push_back(MakeFillItem());
    target.trackMatteType = TrackMatteType::Alpha;
    target.matteSourceId = EntityId{1};
    state.layers.push_back(std::move(source));
    state.layers.push_back(std::move(target));

    auto commands = BuildCommands(state);
    int beginLayerCount = 0;
    int alphaMatteCount = 0;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginLayer) {
            ++beginLayerCount;
        }
        if (command.type == DrawCommandType::BeginMask &&
            command.maskApplyMode == MaskApplyMode::AlphaMatte) {
            ++alphaMatteCount;
        }
    }
    EXPECT_EQ(beginLayerCount, 1);
    EXPECT_EQ(alphaMatteCount, 1);
}

TEST(CommandBuilderTest, TrackMatteReplaysSourceWithRelativeTransform) {
    SceneState state;
    EvaluatedLayer source;
    source.id = EntityId{1};
    source.usedAsMatteOnly = true;
    source.worldTransform = Mat3::Translate(Vec2{10, 0});
    source.shapeItems.push_back(MakeFillItem());
    EvaluatedLayer target;
    target.id = EntityId{2};
    target.worldTransform = Mat3::Translate(Vec2{30, 0});
    target.shapeItems.push_back(MakeFillItem());
    target.trackMatteType = TrackMatteType::LumaInverted;
    target.matteSourceId = EntityId{1};
    state.layers.push_back(std::move(source));
    state.layers.push_back(std::move(target));

    auto commands = BuildCommands(state);
    bool foundRelative = false;
    for (size_t i = 0; i + 1 < commands.size(); ++i) {
        if (commands[i].type == DrawCommandType::BeginMask &&
            commands[i].maskApplyMode == MaskApplyMode::LumaMatteInverted) {
            // BeginMask, Save, ConcatTransform(relative), DrawPath, Restore, EndMask
            ASSERT_LT(i + 3, commands.size());
            EXPECT_EQ(commands[i + 1].type, DrawCommandType::Save);
            EXPECT_EQ(commands[i + 2].type, DrawCommandType::ConcatTransform);
            EXPECT_EQ(commands[i + 2].transform, Mat3::Translate(Vec2{-20, 0}));
            foundRelative = true;
            break;
        }
    }
    EXPECT_TRUE(foundRelative);
}

TEST(CommandBuilderTest, TrackMatteReplaysImageSourceAsDrawImage) {
    SceneState state;
    EvaluatedLayer source;
    source.id = EntityId{1};
    source.usedAsMatteOnly = true;
    source.worldTransform = Mat3::Translate(Vec2{10, 0});
    EvaluatedImageItem image;
    image.absolutePath = "/tmp/project/assets/matte.png";
    image.containerSize = {100, 100};
    image.intrinsicSize = {100, 100};
    image.scaleMode = motion::ImageScaleMode::Stretch;
    source.imageItem = std::move(image);
    EvaluatedLayer target;
    target.id = EntityId{2};
    target.worldTransform = Mat3::Translate(Vec2{30, 0});
    target.shapeItems.push_back(MakeFillItem());
    target.trackMatteType = TrackMatteType::Alpha;
    target.matteSourceId = EntityId{1};
    state.layers.push_back(std::move(source));
    state.layers.push_back(std::move(target));

    auto commands = BuildCommands(state);
    bool foundImageMatte = false;
    for (size_t i = 0; i + 3 < commands.size(); ++i) {
        if (commands[i].type == DrawCommandType::BeginMask &&
            commands[i].maskApplyMode == MaskApplyMode::AlphaMatte) {
            EXPECT_EQ(commands[i + 1].type, DrawCommandType::Save);
            EXPECT_EQ(commands[i + 2].type, DrawCommandType::ConcatTransform);
            EXPECT_EQ(commands[i + 2].transform, Mat3::Translate(Vec2{-20, 0}));
            EXPECT_EQ(commands[i + 3].type, DrawCommandType::DrawImage);
            EXPECT_EQ(commands[i + 3].imagePath, "/tmp/project/assets/matte.png");
            foundImageMatte = true;
            break;
        }
    }
    EXPECT_TRUE(foundImageMatte);
}

TEST(CommandBuilderTest, TrackMatteReplaysGroupDescendants) {
    SceneState state;
    EvaluatedLayer group;
    group.id = EntityId{1};
    group.usedAsMatteOnly = true;
    group.worldTransform = Mat3::Translate(Vec2{10, 0});

    EvaluatedLayer child;
    child.id = EntityId{2};
    child.parentId = EntityId{1};
    child.usedAsMatteOnly = true;
    child.worldTransform = Mat3::Translate(Vec2{10, 0});
    child.shapeItems.push_back(MakeFillItem());

    EvaluatedLayer target;
    target.id = EntityId{3};
    target.worldTransform = Mat3::Translate(Vec2{30, 0});
    target.shapeItems.push_back(MakeFillItem());
    target.trackMatteType = TrackMatteType::Alpha;
    target.matteSourceId = EntityId{1};

    state.layers.push_back(std::move(group));
    state.layers.push_back(std::move(child));
    state.layers.push_back(std::move(target));

    auto commands = BuildCommands(state);
    bool foundDescendantMatte = false;
    for (size_t i = 0; i + 3 < commands.size(); ++i) {
        if (commands[i].type == DrawCommandType::BeginMask &&
            commands[i].maskApplyMode == MaskApplyMode::AlphaMatte) {
            EXPECT_EQ(commands[i + 1].type, DrawCommandType::Save);
            EXPECT_EQ(commands[i + 2].type, DrawCommandType::ConcatTransform);
            EXPECT_EQ(commands[i + 2].transform, Mat3::Translate(Vec2{-20, 0}));
            EXPECT_EQ(commands[i + 3].type, DrawCommandType::DrawPath);
            foundDescendantMatte = true;
            break;
        }
    }
    EXPECT_TRUE(foundDescendantMatte);
}

TEST(CommandBuilderTest, ImageLayerEmitsDrawImage) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedImageItem image;
    image.absolutePath = "/tmp/project/assets/a.png";
    image.containerSize = {200, 100};
    image.intrinsicSize = {400, 200};
    image.scaleMode = motion::ImageScaleMode::LetterBox;
    layer.imageItem = std::move(image);
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 6u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[1].type, DrawCommandType::ConcatTransform);
    EXPECT_EQ(commands[2].type, DrawCommandType::SetOpacity);
    EXPECT_EQ(commands[3].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[4].type, DrawCommandType::DrawImage);
    EXPECT_EQ(commands[4].imagePath, "/tmp/project/assets/a.png");
    EXPECT_FLOAT_EQ(commands[4].imageContainerSize.x, 200.f);
    EXPECT_FLOAT_EQ(commands[4].imageContainerSize.y, 100.f);
    EXPECT_FLOAT_EQ(commands[4].imageIntrinsicSize.x, 400.f);
    EXPECT_FLOAT_EQ(commands[4].imageIntrinsicSize.y, 200.f);
    EXPECT_EQ(commands[4].imageScaleMode, motion::ImageScaleMode::LetterBox);
    EXPECT_EQ(commands[5].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, ImageCornerRadiusEmitsClipPathBeforeDrawImage) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedImageItem image;
    image.absolutePath = "/tmp/project/assets/a.png";
    image.containerSize = {200, 100};
    image.intrinsicSize = {200, 100};
    image.cornerRadius = 12.0f;
    layer.imageItem = std::move(image);
    layer.cornerRadius = 12.0f;
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    int clipIndex = -1;
    int drawIndex = -1;
    int beginLayerCount = 0;
    for (size_t index = 0; index < commands.size(); ++index) {
        if (commands[index].type == DrawCommandType::ClipPath) {
            clipIndex = static_cast<int>(index);
        }
        if (commands[index].type == DrawCommandType::DrawImage) {
            drawIndex = static_cast<int>(index);
        }
        if (commands[index].type == DrawCommandType::BeginLayer) {
            ++beginLayerCount;
        }
    }
    EXPECT_GE(clipIndex, 0);
    EXPECT_GE(drawIndex, 0);
    EXPECT_LT(clipIndex, drawIndex);
    EXPECT_EQ(beginLayerCount, 0);
    EXPECT_EQ(commands[static_cast<size_t>(clipIndex)].geometry.kind, ShapeGeometryKind::Rect);
    EXPECT_FLOAT_EQ(commands[static_cast<size_t>(clipIndex)].geometry.cornerRadius, 12.0f);
}

TEST(CommandBuilderTest, ImageLayerWithoutPathSkipsDrawImage) {
    SceneState state;
    EvaluatedLayer layer;
    EvaluatedImageItem image;
    image.containerSize = {200, 100};
    layer.imageItem = std::move(image);
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    for (const auto &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::DrawImage);
    }
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front().type, DrawCommandType::Save);
    EXPECT_EQ(commands.back().type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, TextLayerEmitsDrawText) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedTextItem text;
    text.text = "Hi";
    text.fontSize = 24.0f;
    text.containerSize = {120, 40};
    text.boxTextMode = true;
    text.align = motion::TextAlign::Center;
    text.fontFamily = "PingFang SC";
    text.fontStyle = "Bold";
    motion::TextDrawStyle fill;
    fill.color = Color{1, 0, 0, 1};
    motion::TextDrawStyle stroke;
    stroke.color = Color{0, 0, 1, 1};
    stroke.isStroke = true;
    stroke.strokeWidth = 1.5f;
    text.styles = {fill, stroke};
    layer.textItem = std::move(text);
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 6u);
    EXPECT_EQ(commands[4].type, DrawCommandType::DrawText);
    EXPECT_EQ(commands[4].textParams.text, "Hi");
    EXPECT_FLOAT_EQ(commands[4].textParams.fontSize, 24.0f);
    EXPECT_FLOAT_EQ(commands[4].textParams.containerSize.x, 120.0f);
    EXPECT_TRUE(commands[4].textParams.boxTextMode);
    EXPECT_EQ(commands[4].textParams.align, motion::TextAlign::Center);
    EXPECT_EQ(commands[4].textParams.fontFamily, "PingFang SC");
    EXPECT_EQ(commands[4].textParams.fontStyle, "Bold");
    ASSERT_EQ(commands[4].textParams.styles.size(), 2u);
    EXPECT_FALSE(commands[4].textParams.styles[0].isStroke);
    EXPECT_FLOAT_EQ(commands[4].textParams.styles[0].color.r, 1.0f);
    EXPECT_TRUE(commands[4].textParams.styles[1].isStroke);
    EXPECT_FLOAT_EQ(commands[4].textParams.styles[1].strokeWidth, 1.5f);
    EXPECT_FALSE(commands[4].textParams.textPathEnabled);
}

TEST(CommandBuilderTest, EffectsForceIsolationAndRideOnEndLayer) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(8.0f);
    layer.effects.push_back(std::move(blur));
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    bool sawBegin = false;
    const DrawCommand *endLayer = nullptr;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginLayer) {
            sawBegin = true;
        }
        if (command.type == DrawCommandType::EndLayer) {
            endLayer = &command;
        }
    }
    EXPECT_TRUE(sawBegin);
    ASSERT_NE(endLayer, nullptr);
    ASSERT_EQ(endLayer->effects.size(), 1u);
    EXPECT_EQ(endLayer->effects[0]->type(), LayerEffectType::GaussianBlur);
}

TEST(CommandBuilderTest, IdentitySkippedEffectsDoNotIsolate) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));
    const auto commands = BuildCommands(state);
    for (const auto &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::BeginLayer);
        EXPECT_NE(command.type, DrawCommandType::EndLayer);
    }
}

TEST(CommandBuilderTest, MaskCommandsStayBeforeEndLayerEffects) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    EvaluatedMask mask;
    mask.mode = MaskMode::Add;
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{5, 0}, {}, {}}}, true);
    mask.path = path;
    layer.masks.push_back(mask);
    auto brightnessContrast = std::make_shared<motion::BrightnessContrastEffect>();
    brightnessContrast->brightness.setStaticValue(20.0f);
    layer.effects.push_back(std::move(brightnessContrast));
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    bool sawBeginMask = false;
    const DrawCommand *endLayer = nullptr;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginMask) {
            sawBeginMask = true;
        }
        if (command.type == DrawCommandType::EndLayer) {
            endLayer = &command;
        }
    }
    EXPECT_TRUE(sawBeginMask);
    ASSERT_NE(endLayer, nullptr);
    ASSERT_EQ(endLayer->effects.size(), 1u);
    EXPECT_EQ(endLayer->effects[0]->type(), LayerEffectType::BrightnessContrast);
}

TEST(CommandBuilderTest, LayerFxForcesIsolationAndRidesOnEndLayer) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    layer.layerStyles.push_back(std::make_shared<DropShadowStyle>());
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    bool sawBegin = false;
    const DrawCommand *endLayer = nullptr;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::BeginLayer) {
            sawBegin = true;
        }
        if (command.type == DrawCommandType::EndLayer) {
            endLayer = &command;
        }
    }
    EXPECT_TRUE(sawBegin);
    ASSERT_NE(endLayer, nullptr);
    ASSERT_EQ(endLayer->layerStyles.size(), 1u);
    EXPECT_EQ(endLayer->layerStyles[0]->type(), LayerFxType::DropShadow);
}

TEST(CommandBuilderTest, LayerFxAndEffectBothRideOnEndLayer) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem());
    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(8.0f);
    layer.effects.push_back(std::move(blur));
    layer.layerStyles.push_back(std::make_shared<DropShadowStyle>());
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    const DrawCommand *endLayer = nullptr;
    for (const auto &command : commands) {
        if (command.type == DrawCommandType::EndLayer) {
            endLayer = &command;
        }
    }
    ASSERT_NE(endLayer, nullptr);
    ASSERT_EQ(endLayer->effects.size(), 1u);
    ASSERT_EQ(endLayer->layerStyles.size(), 1u);
    EXPECT_EQ(endLayer->effects[0]->type(), LayerEffectType::GaussianBlur);
    EXPECT_EQ(endLayer->layerStyles[0]->type(), LayerFxType::DropShadow);
}

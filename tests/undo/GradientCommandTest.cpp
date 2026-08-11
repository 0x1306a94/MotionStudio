#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/undo/AddGradientStopCommand.h"
#include "MotionStudio/undo/RemoveGradientStopCommand.h"
#include "MotionStudio/undo/SetGradientTypeCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::AddGradientStopCommand;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EnsureDefaultGradient;
using motion::FillStyle;
using motion::GradientStopsAreValid;
using motion::GradientType;
using motion::Layer;
using motion::LayerType;
using motion::RemoveGradientStopCommand;
using motion::SetGradientTypeCommand;
using motion::UndoManager;
using motion::Vec2;

namespace {

struct Scene {
    Document document;
    UndoManager undo;
    Layer *layer = nullptr;

    Scene() {
        Composition *composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        auto fill = std::make_unique<FillStyle>();
        EnsureDefaultGradient(fill->gradient, Vec2{0, 0}, Vec2{100, 0});
        layer->styles.push_back(std::move(fill));
    }

    FillStyle &fill() {
        return static_cast<FillStyle &>(*layer->styles[0]);
    }

    template <typename CommandType, typename... Args>
    void execute(Args &&...args) {
        undo.execute(document, std::make_unique<CommandType>(std::forward<Args>(args)...));
    }
};

}  // namespace

TEST(GradientCommandTest, SetTypeUndoRedo) {
    Scene scene;
    scene.execute<SetGradientTypeCommand>(scene.layer->id, 0, GradientType::Conic);
    EXPECT_EQ(scene.fill().gradient.type, GradientType::Conic);
    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.fill().gradient.type, GradientType::Linear);
    scene.undo.redo(scene.document);
    EXPECT_EQ(scene.fill().gradient.type, GradientType::Conic);
}

TEST(GradientCommandTest, AddAndRemoveStop) {
    Scene scene;
    scene.execute<AddGradientStopCommand>(scene.layer->id, 0, 1, Color{1, 0, 0, 1}, 0.5f);
    ASSERT_EQ(scene.fill().gradient.stops.size(), 3u);
    EXPECT_TRUE(GradientStopsAreValid(scene.fill().gradient));
    EXPECT_EQ(scene.fill().gradient.stops[1].color.staticValue(), (Color{1, 0, 0, 1}));

    scene.execute<RemoveGradientStopCommand>(scene.layer->id, 0, 1);
    EXPECT_EQ(scene.fill().gradient.stops.size(), 2u);
    EXPECT_TRUE(GradientStopsAreValid(scene.fill().gradient));

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.fill().gradient.stops.size(), 3u);
    EXPECT_EQ(scene.fill().gradient.stops[1].color.staticValue(), (Color{1, 0, 0, 1}));
}

TEST(GradientCommandTest, RemoveRejectedWhenOnlyTwoStops) {
    Scene scene;
    scene.execute<RemoveGradientStopCommand>(scene.layer->id, 0, 0);
    EXPECT_EQ(scene.fill().gradient.stops.size(), 2u);
}

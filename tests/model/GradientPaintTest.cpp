#include <gtest/gtest.h>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StylePaintMode.h"

using motion::Color;
using motion::FillStyle;
using motion::GradientType;
using motion::StylePaintMode;

TEST(GradientPaintTest, DefaultsAndStyleHoldsGradient) {
    FillStyle fill;
    EXPECT_EQ(fill.paintMode, StylePaintMode::Color);
    EXPECT_EQ(fill.gradient.type, GradientType::Linear);
    EXPECT_TRUE(fill.gradient.stops.empty());
    fill.paintMode = StylePaintMode::Gradient;
    fill.gradient.stops.push_back({});
    fill.gradient.stops[0].color.setStaticValue(Color{1, 0, 0, 1});
    fill.gradient.stops[0].position.setStaticValue(0.f);
    EXPECT_EQ(fill.paintMode, StylePaintMode::Gradient);
    EXPECT_EQ(fill.gradient.stops.size(), 1u);
}

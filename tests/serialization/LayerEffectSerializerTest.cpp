#include <memory>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/serialization/Serializer.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::BrightnessContrastEffect;
using motion::Composition;
using motion::Document;
using motion::Expected;
using motion::GaussianBlurEffect;
using motion::Keyframe;
using motion::Layer;
using motion::LayerEffectType;
using motion::LayerType;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::Serializer;
using motion::ShapeContent;
using motion::ShapeRect;

namespace {

struct EffectDocument {
    Document document;
    Layer *layer = nullptr;

    EffectDocument() {
        Composition *composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        composition->width = 100;
        composition->height = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        content->geometry = std::make_unique<ShapeRect>();
        layer->styles.push_back(std::make_unique<motion::FillStyle>());

        auto brightnessContrast = std::make_unique<BrightnessContrastEffect>();
        brightnessContrast->brightness.addKeyframe(Keyframe<float>{0, 10.0f});
        brightnessContrast->brightness.addKeyframe(Keyframe<float>{10, 40.0f});
        layer->effects.push_back(std::move(brightnessContrast));

        auto blur = std::make_unique<GaussianBlurEffect>();
        blur->blurriness.setStaticValue(8.0f);
        blur->repeatEdgePixels = true;
        layer->effects.push_back(std::move(blur));
    }
};

}  // namespace

TEST(LayerEffectSerializerTest, RoundTripKeepsBothEffectTypes) {
    EffectDocument scene;
    const std::string first = Serializer::serialize(scene.document);
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(first);
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ((*restored)->compositions[0]->layers[0]->effects.size(), 2u);
    EXPECT_EQ((*restored)->compositions[0]->layers[0]->effects[0]->type(),
              LayerEffectType::BrightnessContrast);
    const auto *blur = static_cast<const GaussianBlurEffect *>(
        (*restored)->compositions[0]->layers[0]->effects[1].get());
    EXPECT_TRUE(blur->repeatEdgePixels);
    EXPECT_FLOAT_EQ(blur->blurriness.evaluate(0), 8.0f);
    EXPECT_EQ(Serializer::serialize(**restored), first);
}

TEST(LayerEffectSerializerTest, MissingEffectsArrayLoadsEmpty) {
    EffectDocument scene;
    auto json = nlohmann::json::parse(Serializer::serialize(scene.document));
    json["compositions"][0]["layers"][0].erase("effects");
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    EXPECT_TRUE((*restored)->compositions[0]->layers[0]->effects.empty());
}

TEST(LayerEffectSerializerTest, UnknownTypeIsSkipped) {
    EffectDocument scene;
    auto json = nlohmann::json::parse(Serializer::serialize(scene.document));
    json["compositions"][0]["layers"][0]["effects"] = nlohmann::json::array({
        {{"id", "aaaaaaaaaaaaaaaa"}, {"type", "unknownEffect"}},
        json["compositions"][0]["layers"][0]["effects"][1],
    });
    Expected<std::unique_ptr<Document>, std::string> restored = Serializer::deserialize(json.dump());
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ((*restored)->compositions[0]->layers[0]->effects.size(), 1u);
    EXPECT_EQ((*restored)->compositions[0]->layers[0]->effects[0]->type(),
              LayerEffectType::GaussianBlur);
}

TEST(LayerEffectSerializerTest, ResolvesBrightnessPropertyPath) {
    EffectDocument scene;
    auto *brightnessContrast = static_cast<BrightnessContrastEffect *>(scene.layer->effects[0].get());
    brightnessContrast->brightness = Animatable<float>{};
    brightnessContrast->brightness.setStaticValue(0.0f);
    PropertyPath path{scene.layer->id, "effects[0].brightness"};
    AnimatableBase *base = ResolveAnimatable(scene.document, path);
    ASSERT_NE(base, nullptr);
    static_cast<Animatable<float> *>(base)->setStaticValue(33.0f);
    EXPECT_FLOAT_EQ(brightnessContrast->brightness.evaluate(0), 33.0f);
}

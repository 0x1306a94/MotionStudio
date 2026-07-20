// Standalone performance benchmark: evaluates a 100-layer x 50-keyframe scene
// frame by frame and reports ms/frame. CI records the number only and never
// gates on it (CTest label "benchmark"); the budget is M2's < 2 ms acceptance
// measured in Release on Apple Silicon.
#include <chrono>
#include <cstdio>
#include <memory>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::ShapeRect;
using motion::Vec2;

namespace {

constexpr int kLayerCount = 100;
constexpr int kKeyframesPerProperty = 50;
constexpr int kFrameCount = 100;
constexpr int kRepetitions = 10;
constexpr double kBudgetMs = 2.0;

Document BuildScene() {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = kFrameCount;
    for (int i = 0; i < kLayerCount; ++i) {
        Layer *layer =
            document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = kFrameCount;
        for (int k = 0; k < kKeyframesPerProperty; ++k) {
            Keyframe<Vec2> keyframe;
            keyframe.time = FrameTime(k * kFrameCount / kKeyframesPerProperty);
            keyframe.value = Vec2{float(k % 2 == 0 ? 0 : 400), float(i)};
            keyframe.easing = Easing::EaseOut();
            layer->transform.position.addKeyframe(keyframe);
        }
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto rect = std::make_unique<ShapeRect>();
        rect->position.setStaticValue(Vec2{0, float(i)});
        rect->size.setStaticValue(Vec2{50, 8});
        content->elements.push_back(std::move(rect));
        content->elements.push_back(std::make_unique<ShapeFill>());
    }
    return document;
}

}  // namespace

int main() {
    const Document document = BuildScene();
    const EntityId compositionId = document.compositions[0]->id;

    // Warmup pass.
    for (FrameTime time = 0; time < kFrameCount; ++time) {
        (void)SceneEvaluator::Evaluate(document, compositionId, time);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int repetition = 0; repetition < kRepetitions; ++repetition) {
        for (FrameTime time = 0; time < kFrameCount; ++time) {
            (void)SceneEvaluator::Evaluate(document, compositionId, time);
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double msPerFrame =
        std::chrono::duration<double, std::milli>(elapsed).count() /
        double(kRepetitions * kFrameCount);

    std::printf("scene evaluation: %.3f ms/frame (%d layers x %d keyframes, budget "
                "%.3f ms) %s\n",
                msPerFrame, kLayerCount, kKeyframesPerProperty, kBudgetMs,
                msPerFrame <= kBudgetMs ? "[within budget]" : "[over budget]");
    return 0;
}

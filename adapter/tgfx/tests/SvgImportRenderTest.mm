#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/RenderAdapter.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/undo/UndoManager.h"

#include "TgfxRenderAdapter.h"
#include "TgfxTestGPUEnvironment.h"

#include <tgfx/core/Color.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Size.h>
#include <tgfx/core/Stream.h>
#include <tgfx/svg/SVGDOM.h>

using motion::BuildCommands;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::PlayCommands;
using motion::SceneEvaluator;
using motion::TgfxRenderAdapter;
using motion::UndoManager;
using motion::svg::ImportSvgFileInto;
using tgfx_test::OutputPath;
using tgfx_test::SaveWebp;
using tgfx_test::TgfxTestGPUEnvironment;

namespace {

std::string FixturePath() {
    return (std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "tests" /
            "import" / "svg" / "fixtures" / "kitchen_sink.svg")
        .lexically_normal()
        .string();
}

bool HasNonBackgroundPixel(const std::vector<uint8_t> &pixels, uint8_t bgR, uint8_t bgG,
                           uint8_t bgB) {
    const size_t count = pixels.size() / 4;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t r = pixels[i * 4];
        const uint8_t g = pixels[i * 4 + 1];
        const uint8_t b = pixels[i * 4 + 2];
        if (r != bgR || g != bgG || b != bgB) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(SvgImportRenderTest, RendersKitchenSinkToWebp) {
    constexpr int kSize = 256;
    auto adapter = TgfxRenderAdapter::Make(kSize, kSize);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = kSize;
    composition->height = kSize;
    composition->duration = 1;
    composition->backgroundColor = Color{0.95f, 0.95f, 0.95f, 1.f};
    auto *host = document.addComposition(std::move(composition));
    ASSERT_NE(host, nullptr);

    UndoManager undo;
    const auto imported = ImportSvgFileInto(document, undo, host->id, FixturePath());
    ASSERT_TRUE(imported.hasValue()) << imported.error();

    const auto state = SceneEvaluator::Evaluate(document, host->id, 0);
    ASSERT_TRUE(state.hasValue());
    adapter->beginFrame(kSize, kSize, state.value().backgroundColor, state.value().cornerRadius);
    PlayCommands(BuildCommands(state.value()), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const std::string importedPath = OutputPath("SvgImport_KitchenSink_Imported.webp");
    ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, importedPath)) << "failed to save " << importedPath;
    EXPECT_TRUE(HasNonBackgroundPixel(pixels, 242, 242, 242))
        << "imported frame looks empty; inspect " << importedPath;

    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    auto stream = tgfx::Stream::MakeFromFile(FixturePath());
    ASSERT_NE(stream, nullptr);
    auto dom = tgfx::SVGDOM::Make(*stream);
    ASSERT_NE(dom, nullptr);
    auto *canvas = env->surface()->getCanvas();
    canvas->clear(tgfx::Color::FromRGBA(242, 242, 242, 255));
    dom->setContainerSize(tgfx::Size::Make(static_cast<float>(kSize), static_cast<float>(kSize)));
    dom->render(canvas);

    tgfx::ImageInfo info =
        tgfx::ImageInfo::Make(kSize, kSize, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    std::vector<uint8_t> refPixels(static_cast<size_t>(info.rowBytes() * info.height()));
    ASSERT_TRUE(env->surface()->readPixels(info, refPixels.data()));
    const std::string refPath = OutputPath("SvgImport_KitchenSink_SvgDom.webp");
    ASSERT_TRUE(SaveWebp(refPixels, kSize, kSize, refPath)) << "failed to save " << refPath;
    env->unlockContext();
}

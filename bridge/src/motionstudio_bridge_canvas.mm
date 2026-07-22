#include "motionstudio_bridge.h"

#include <utility>

#include "MSCanvasInternal.h"

MSCanvas *ms_canvas_create(void *mtkView) {
    auto adapter = motion::TgfxOnScreenAdapter::Make(mtkView);
    if (!adapter) {
        return nullptr;
    }
    auto *canvas = new MSCanvas();
    canvas->adapter = std::move(adapter);
    return canvas;
}

void ms_canvas_destroy(MSCanvas *canvas) {
    delete canvas;
}

void ms_canvas_set_preview_backdrop(MSCanvas *canvas, int backdrop) {
    if (canvas == nullptr || canvas->adapter == nullptr) {
        return;
    }
    const auto mode = backdrop == 1 ? motion::PreviewBackdrop::Transparent
                                    : motion::PreviewBackdrop::Black;
    canvas->adapter->setPreviewBackdrop(mode);
}

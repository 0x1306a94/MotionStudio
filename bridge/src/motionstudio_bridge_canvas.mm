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

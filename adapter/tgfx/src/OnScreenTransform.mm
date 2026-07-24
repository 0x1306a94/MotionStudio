#include "OnScreenTransform.h"

#include <algorithm>
#include <cmath>

namespace motion {

ScreenTransform MakeOnScreenTransform(int sceneWidth, int sceneHeight, float targetWidth, float targetHeight,
                                      float zoom, float panX, float panY, float contentsScale) {
    ScreenTransform transform = {};
    if (sceneWidth <= 0 || sceneHeight <= 0 || targetWidth <= 0.0f || targetHeight <= 0.0f) {
        return transform;
    }
    // Fit Up to 100% (AE Composition panel default): scale down to fit when the
    // drawable is smaller than the composition; never scale above 1:1.
    const float fitScale = std::min(1.0f, std::min(targetWidth / float(sceneWidth), targetHeight / float(sceneHeight)));
    // Map onto a whole-pixel destination rect so AA edges don't sit on half
    // pixels (which previously produced dark left/top fringes with Src).
    const int destWidth = std::max(1, int(std::floor(float(sceneWidth) * fitScale + 1e-6f)));
    const int destHeight = std::max(1, int(std::floor(float(sceneHeight) * fitScale + 1e-6f)));
    const float offsetX = std::floor((targetWidth - float(destWidth)) * 0.5f);
    const float offsetY = std::floor((targetHeight - float(destHeight)) * 0.5f);
    const float fitScaleX = float(destWidth) / float(sceneWidth);
    const float fitScaleY = float(destHeight) / float(sceneHeight);

    const float safeZoom = zoom > 0.0f ? zoom : 1.0f;
    const float pointScale = contentsScale > 0.0f ? contentsScale : 1.0f;
    // User transform T(pan) * S(zoom) composed over fit T(offset) * S(fit):
    // scale-and-translate composes component-wise, no skew ever introduced.
    transform.scaleX = safeZoom * fitScaleX;
    transform.scaleY = safeZoom * fitScaleY;
    transform.translateX = panX * pointScale + safeZoom * offsetX;
    transform.translateY = panY * pointScale + safeZoom * offsetY;
    return transform;
}

}  // namespace motion

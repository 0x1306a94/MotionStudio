#pragma once

namespace motion {

// Row-major affine transform mapping scene coordinates to drawable (pixel)
// coordinates: [scaleX skewX translateX; skewY scaleY translateY].
struct ScreenTransform {
    float scaleX = 1;
    float skewX = 0;
    float translateX = 0;
    float skewY = 0;
    float scaleY = 1;
    float translateY = 0;
};

// Computes the full scene->drawable transform of the on-screen preview canvas.
// The scene is first fit into the drawable (AE "Fit Up to 100%": scaled down
// to fit, letterboxed, centered, never above 1:1, destination rect snapped to
// whole pixels), then the user view transform is applied on top: a uniform
// zoom about the drawable origin followed by a pan.
// sceneWidth: composition width in scene units (must be > 0).
// sceneHeight: composition height in scene units (must be > 0).
// targetWidth: drawable width in pixels.
// targetHeight: drawable height in pixels.
// zoom: magnification relative to fit (1 = fit). Non-positive values fall
//       back to 1 so the matrix never degenerates.
// panX: horizontal pan in view points.
// panY: vertical pan in view points.
// contentsScale: view points -> drawable pixels factor (non-positive values
//                fall back to 1).
ScreenTransform MakeOnScreenTransform(int sceneWidth, int sceneHeight, float targetWidth, float targetHeight,
                                      float zoom, float panX, float panY, float contentsScale);

}  // namespace motion

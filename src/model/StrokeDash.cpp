#include "MotionStudio/model/StrokeDash.h"

#include <algorithm>

namespace motion {

std::vector<float> NormalizeDashArray(std::vector<float> dashes) {
    for (float &value : dashes) {
        value = std::max(0.0f, value);
    }
    if (dashes.size() % 2 == 1) {
        dashes.insert(dashes.end(), dashes.begin(), dashes.end());
    }
    float sum = 0.0f;
    for (float value : dashes) {
        sum += value;
    }
    if (dashes.size() < 2 || sum <= 0.0f) {
        return {};
    }
    return dashes;
}

bool NeedsDash(StrokeMode strokeMode, const std::vector<float> &dashes) {
    return strokeMode == StrokeMode::Dashed && !NormalizeDashArray(dashes).empty();
}

std::vector<float> DefaultDashPattern() {
    return {8.0f, 8.0f};
}

}  // namespace motion

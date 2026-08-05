#include "MotionStudio/animation/MotionPath.h"

namespace motion {

BezierPath BuildMotionPath(const Animatable<Vec2> &position) {
    const std::vector<Keyframe<Vec2>> &keyframes = position.keyframes();
    if (keyframes.size() < 2) {
        return {};
    }

    std::vector<BezierPath::Vertex> vertices;
    vertices.reserve(keyframes.size());
    for (size_t index = 0; index < keyframes.size(); ++index) {
        BezierPath::Vertex vertex;
        vertex.point = keyframes[index].value;

        if (index + 1 < keyframes.size() && keyframes[index].spatialOutTangent &&
            keyframes[index + 1].spatialInTangent) {
            vertex.outTangent = *keyframes[index].spatialOutTangent;
        }
        if (index > 0 && keyframes[index - 1].spatialOutTangent &&
            keyframes[index].spatialInTangent) {
            vertex.inTangent = *keyframes[index].spatialInTangent;
        }
        vertices.push_back(vertex);
    }
    return MakeSingleContour(std::move(vertices), false);
}

}  // namespace motion

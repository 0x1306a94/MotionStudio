#include "MotionStudio/animation/MotionPath.h"

namespace motion {

BezierPath BuildMotionPath(const Animatable<Vec2> &position) {
    const std::vector<Keyframe<Vec2>> &keyframes = position.keyframes();
    BezierPath path;
    path.closed = false;
    if (keyframes.size() < 2) {
        return path;
    }

    path.vertices.reserve(keyframes.size());
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
        path.vertices.push_back(vertex);
    }
    return path;
}

}  // namespace motion

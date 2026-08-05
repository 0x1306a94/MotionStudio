#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Converts a legacy single-ring BezierPath (vertices + closed) into a network.
// Vertex ids are 1..N in order; edge ids are 1..edgeCount.
VectorNetwork BezierPathToVectorNetwork(const BezierPath &path);

// Reconstructs a single closed/open ring when the network is exactly one simple
// cycle or open chain in vertex order. Returns empty path when topology is not
// a single simple ring.
BezierPath VectorNetworkToSingleRingBezierPath(const VectorNetwork &network);

}  // namespace motion

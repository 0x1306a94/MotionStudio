//
//  LayerAlign.swift
//  MotionStudioApp
//
//  Composition-space alignment helpers for selected layer visual AABBs.
//

import CoreGraphics
import Foundation

enum LayerAlignEdge {
    case left, horizontalCenter, right
    case top, verticalCenter, bottom
}

enum LayerAlign {
    static func unionBounds(_ rects: [CGRect]) -> CGRect? {
        guard let first = rects.first else { return nil }
        return rects.dropFirst().reduce(first) { $0.union($1) }
    }

    static func compositionDelta(edge: LayerAlignEdge,
                                 bounds: CGRect,
                                 target: CGRect) -> CGVector
    {
        switch edge {
        case .left:
            CGVector(dx: target.minX - bounds.minX, dy: 0)
        case .horizontalCenter:
            CGVector(dx: target.midX - bounds.midX, dy: 0)
        case .right:
            CGVector(dx: target.maxX - bounds.maxX, dy: 0)
        case .top:
            CGVector(dx: 0, dy: target.minY - bounds.minY)
        case .verticalCenter:
            CGVector(dx: 0, dy: target.midY - bounds.midY)
        case .bottom:
            CGVector(dx: 0, dy: target.maxY - bounds.maxY)
        }
    }
}

//
//  LayoutPosition.swift
//  MotionStudioApp
//
//  UI presentation helpers: stored AE position (anchor in parent space)
//  ↔ layout position (local AABB top-left in parent space).
//

import CoreGraphics
import Foundation

enum LayoutPosition {
    /// Parent-space vector from local AABB top-left to the anchor:
    /// `R · S · (anchor − bounds.min)`. Empty / null bounds → `.zero`.
    ///
    /// With AE local matrix `T(position)·R·S·T(-anchor)`:
    /// `layoutTopLeft = storedPosition − offset`.
    static func offset(anchor: CGVector,
                       scale: CGVector,
                       rotationDegrees: Float,
                       localBounds: CGRect) -> CGVector
    {
        guard !localBounds.isNull, !localBounds.isInfinite,
              localBounds.width > 0, localBounds.height > 0
        else {
            return .zero
        }
        let delta = CGPoint(x: anchor.dx - localBounds.minX,
                            y: anchor.dy - localBounds.minY)
        let scaled = CGPoint(x: delta.x * scale.dx, y: delta.y * scale.dy)
        let radians = CGFloat(rotationDegrees) * .pi / 180
        let cosine = cos(radians)
        let sine = sin(radians)
        return CGVector(dx: scaled.x * cosine - scaled.y * sine,
                        dy: scaled.x * sine + scaled.y * cosine)
    }

    static func toLayout(stored: CGVector, offset: CGVector) -> CGVector {
        CGVector(dx: stored.dx - offset.dx, dy: stored.dy - offset.dy)
    }

    static func toStored(layout: CGVector, offset: CGVector) -> CGVector {
        CGVector(dx: layout.dx + offset.dx, dy: layout.dy + offset.dy)
    }
}

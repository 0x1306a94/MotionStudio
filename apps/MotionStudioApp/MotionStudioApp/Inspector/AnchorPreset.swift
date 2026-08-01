//
//  AnchorPreset.swift
//  MotionStudioApp
//

import CoreGraphics
import Foundation

enum AnchorPresetCorner: CaseIterable, Hashable {
    case topLeft, topCenter, topRight
    case middleLeft, center, middleRight
    case bottomLeft, bottomCenter, bottomRight
}

enum AnchorPreset {
    static func point(corner: AnchorPresetCorner, in rect: CGRect) -> CGVector {
        let x: CGFloat
        let y: CGFloat
        switch corner {
        case .topLeft, .middleLeft, .bottomLeft:
            x = rect.minX
        case .topCenter, .center, .bottomCenter:
            x = rect.midX
        case .topRight, .middleRight, .bottomRight:
            x = rect.maxX
        }
        switch corner {
        case .topLeft, .topCenter, .topRight:
            y = rect.minY
        case .middleLeft, .center, .middleRight:
            y = rect.midY
        case .bottomLeft, .bottomCenter, .bottomRight:
            y = rect.maxY
        }
        return CGVector(dx: x, dy: y)
    }

    static func matchingCorner(anchor: CGVector,
                               rect: CGRect,
                               tolerance: CGFloat = 0.5) -> AnchorPresetCorner?
    {
        for corner in AnchorPresetCorner.allCases {
            let preset = point(corner: corner, in: rect)
            if abs(preset.dx - anchor.dx) <= tolerance, abs(preset.dy - anchor.dy) <= tolerance {
                return corner
            }
        }
        return nil
    }

    /// Δlocal = new − old; Δscene = rotate(scale(Δlocal)); return oldPosition + Δscene.
    static func compensatedPosition(oldAnchor: CGVector,
                                    newAnchor: CGVector,
                                    position: CGVector,
                                    scale: CGVector,
                                    rotationDegrees: Float) -> CGVector
    {
        let deltaLocal = CGPoint(x: newAnchor.dx - oldAnchor.dx, y: newAnchor.dy - oldAnchor.dy)
        let scaled = CGPoint(x: deltaLocal.x * scale.dx, y: deltaLocal.y * scale.dy)
        let radians = CGFloat(rotationDegrees) * .pi / 180
        let cosine = cos(radians)
        let sine = sin(radians)
        let rotated = CGPoint(x: scaled.x * cosine - scaled.y * sine,
                              y: scaled.x * sine + scaled.y * cosine)
        return CGVector(dx: position.dx + rotated.x, dy: position.dy + rotated.y)
    }
}

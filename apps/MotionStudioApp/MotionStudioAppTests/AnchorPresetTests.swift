//
//  AnchorPresetTests.swift
//  MotionStudioAppTests
//

import CoreGraphics
import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct AnchorPresetTests {
    @Test
    func `nine points cover rect corners edges and center`() {
        let rect = CGRect(x: 10, y: 20, width: 100, height: 40)
        #expect(AnchorPreset.point(corner: .topLeft, in: rect) == CGVector(dx: 10, dy: 20))
        #expect(AnchorPreset.point(corner: .topCenter, in: rect) == CGVector(dx: 60, dy: 20))
        #expect(AnchorPreset.point(corner: .topRight, in: rect) == CGVector(dx: 110, dy: 20))
        #expect(AnchorPreset.point(corner: .middleLeft, in: rect) == CGVector(dx: 10, dy: 40))
        #expect(AnchorPreset.point(corner: .center, in: rect) == CGVector(dx: 60, dy: 40))
        #expect(AnchorPreset.point(corner: .middleRight, in: rect) == CGVector(dx: 110, dy: 40))
        #expect(AnchorPreset.point(corner: .bottomLeft, in: rect) == CGVector(dx: 10, dy: 60))
        #expect(AnchorPreset.point(corner: .bottomCenter, in: rect) == CGVector(dx: 60, dy: 60))
        #expect(AnchorPreset.point(corner: .bottomRight, in: rect) == CGVector(dx: 110, dy: 60))
    }

    @Test
    func `matching corner uses tolerance`() {
        let rect = CGRect(x: 0, y: 0, width: 200, height: 100)
        #expect(AnchorPreset.matchingCorner(anchor: CGVector(dx: 100.2, dy: 50.1),
                                            rect: rect,
                                            tolerance: 0.5) == .center)
        #expect(AnchorPreset.matchingCorner(anchor: CGVector(dx: 30, dy: 30),
                                            rect: rect,
                                            tolerance: 0.5) == nil)
    }

    @Test
    func `compensated position keeps content fixed under rotation and scale`() {
        // oldAnchor (0,0) → newAnchor (100,0); scale (2,1); rotation 90° CCW
        // Δlocal=(100,0) → scaled=(200,0) → rotated=(0,200)
        let newPosition = AnchorPreset.compensatedPosition(
            oldAnchor: CGVector(dx: 0, dy: 0),
            newAnchor: CGVector(dx: 100, dy: 0),
            position: CGVector(dx: 50, dy: 50),
            scale: CGVector(dx: 2, dy: 1),
            rotationDegrees: 90,
        )
        #expect(abs(newPosition.dx - 50) < 1e-4)
        #expect(abs(newPosition.dy - 250) < 1e-4)
    }
}

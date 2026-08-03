//
//  LayoutPositionTests.swift
//  MotionStudioAppTests
//

import CoreGraphics
import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct LayoutPositionTests {
    @Test
    func `image center anchor maps layout to top left`() {
        let bounds = CGRect(x: 0, y: 0, width: 200, height: 200)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: bounds)
        #expect(abs(offset.dx - 100) < 1e-4)
        #expect(abs(offset.dy - 100) < 1e-4)
        let layout = LayoutPosition.toLayout(stored: CGVector(dx: 200, dy: 200), offset: offset)
        #expect(abs(layout.dx - 100) < 1e-4)
        #expect(abs(layout.dy - 100) < 1e-4)
    }

    @Test
    func `recentered shape min corner offset`() {
        let bounds = CGRect(x: -50, y: -40, width: 100, height: 80)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 0, dy: 0),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: bounds)
        #expect(abs(offset.dx - 50) < 1e-4)
        #expect(abs(offset.dy - 40) < 1e-4)
        let layout = LayoutPosition.toLayout(stored: CGVector(dx: 300, dy: 300), offset: offset)
        #expect(abs(layout.dx - 250) < 1e-4)
        #expect(abs(layout.dy - 260) < 1e-4)
    }

    @Test
    func `write layout zero stores position at offset`() {
        let offset = CGVector(dx: 100, dy: 50)
        let stored = LayoutPosition.toStored(layout: .zero, offset: offset)
        #expect(abs(stored.dx - 100) < 1e-4)
        #expect(abs(stored.dy - 50) < 1e-4)
        let roundTrip = LayoutPosition.toLayout(stored: stored, offset: offset)
        #expect(abs(roundTrip.dx) < 1e-4)
        #expect(abs(roundTrip.dy) < 1e-4)
    }

    @Test
    func `rotation and scale transform anchor minus min`() {
        // bounds.min=(0,0), anchor=(10,0), scale=(2,1), rot=90° CCW
        // delta=(10,0) → scaled=(20,0) → rotated=(0,20)
        let bounds = CGRect(x: 0, y: 0, width: 40, height: 20)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 10, dy: 0),
                                           scale: CGVector(dx: 2, dy: 1),
                                           rotationDegrees: 90,
                                           localBounds: bounds)
        #expect(abs(offset.dx - 0) < 1e-4)
        #expect(abs(offset.dy - 20) < 1e-4)
    }

    @Test
    func `empty bounds yield zero offset`() {
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: .null)
        #expect(offset == .zero)
        let empty = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                          scale: CGVector(dx: 1, dy: 1),
                                          rotationDegrees: 0,
                                          localBounds: CGRect(x: 0, y: 0, width: 0, height: 10))
        #expect(empty == .zero)
    }
}

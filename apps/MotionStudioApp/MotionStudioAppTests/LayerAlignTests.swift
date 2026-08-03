//
//  LayerAlignTests.swift
//  MotionStudioAppTests
//

import CoreGraphics
@testable import MotionStudio
import Testing

@MainActor
struct LayerAlignTests {
    @Test
    func `union bounds of two rects`() {
        let a = CGRect(x: 0, y: 0, width: 10, height: 10)
        let b = CGRect(x: 20, y: 30, width: 10, height: 10)
        let union = LayerAlign.unionBounds([a, b])
        #expect(union == CGRect(x: 0, y: 0, width: 30, height: 40))
    }

    @Test
    func `union bounds empty is nil`() {
        #expect(LayerAlign.unionBounds([]) == nil)
    }

    @Test
    func `six edges composition delta`() {
        let bounds = CGRect(x: 10, y: 20, width: 40, height: 60)
        let target = CGRect(x: 0, y: 0, width: 200, height: 100)
        expectNear(LayerAlign.compositionDelta(edge: .left, bounds: bounds, target: target),
                   CGVector(dx: -10, dy: 0))
        expectNear(LayerAlign.compositionDelta(edge: .horizontalCenter, bounds: bounds, target: target),
                   CGVector(dx: 70, dy: 0))
        expectNear(LayerAlign.compositionDelta(edge: .right, bounds: bounds, target: target),
                   CGVector(dx: 150, dy: 0))
        expectNear(LayerAlign.compositionDelta(edge: .top, bounds: bounds, target: target),
                   CGVector(dx: 0, dy: -20))
        expectNear(LayerAlign.compositionDelta(edge: .verticalCenter, bounds: bounds, target: target),
                   CGVector(dx: 0, dy: 0))
        expectNear(LayerAlign.compositionDelta(edge: .bottom, bounds: bounds, target: target),
                   CGVector(dx: 0, dy: 20))
    }

    @Test
    func `already aligned yields zero`() {
        let bounds = CGRect(x: 0, y: 0, width: 50, height: 50)
        let target = CGRect(x: 0, y: 0, width: 200, height: 200)
        expectNear(LayerAlign.compositionDelta(edge: .left, bounds: bounds, target: target), .zero)
        expectNear(LayerAlign.compositionDelta(edge: .top, bounds: bounds, target: target), .zero)
    }

    private func expectNear(_ actual: CGVector, _ expected: CGVector, tolerance: CGFloat = 1e-4) {
        #expect(abs(actual.dx - expected.dx) < tolerance)
        #expect(abs(actual.dy - expected.dy) < tolerance)
    }
}

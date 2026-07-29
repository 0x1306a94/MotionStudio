//
//  TimelineDragEngineTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct TimelineDragEngineTests {
    @Test
    func `layer scale trailing matches figma sample`() throws {
        let session = try #require(TimelineDragEngine.makeLayerScaleSession(
            edge: .trailing,
            originStart: 0,
            originEnd: 600,
            originFrames: [
                ("p", 0), ("p", 492), ("p", 600),
            ],
        ))
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: 802,
                                               duration: 10000,
                                               neighbors: nil)
        let byFrom = Dictionary(uniqueKeysWithValues: moves.map { ($0.from, $0.to) })
        #expect(byFrom[0] == nil)
        #expect(byFrom[492] == 658)
        #expect(byFrom[600] == 802)
    }

    @Test
    func `layer scale leading mirrors trailing`() throws {
        let session = try #require(TimelineDragEngine.makeLayerScaleSession(
            edge: .leading,
            originStart: 100,
            originEnd: 500,
            originFrames: [
                ("a", 100), ("a", 300), ("a", 500),
            ],
        ))
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: 0,
                                               duration: 10000,
                                               neighbors: nil)
        let byFrom = Dictionary(uniqueKeysWithValues: moves.map { ($0.from, $0.to) })
        #expect(byFrom[500] == nil)
        #expect(byFrom[100] == 0)
        #expect(byFrom[300] == 250)
    }

    @Test
    func `property edge only moves edge frame`() throws {
        let session = try #require(TimelineDragEngine.makePropertyEdgeSession(
            path: "transform.position",
            edge: .trailing,
            originStart: 10,
            originEnd: 40,
        ))
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: 80,
                                               duration: 100,
                                               neighbors: nil)
        #expect(moves == [KeyframeMove(path: "transform.position", from: 40, to: 80)])
    }

    @Test
    func `keyframe clamped by neighbors`() {
        let session = TimelineDragEngine.makeKeyframeSession(path: "p", frame: 20)
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: 100,
                                               duration: 200,
                                               neighbors: (prev: 10, next: 30))
        #expect(moves == [KeyframeMove(path: "p", from: 20, to: 29)])
    }

    @Test
    func `keyframe clamped to duration when no neighbors`() {
        let session = TimelineDragEngine.makeKeyframeSession(path: "p", frame: 5)
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: -10,
                                               duration: 50,
                                               neighbors: (prev: nil, next: nil))
        #expect(moves == [KeyframeMove(path: "p", from: 5, to: 0)])
    }
}

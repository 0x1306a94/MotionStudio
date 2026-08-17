//
//  TimelineFrameClampTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct TimelineFrameClampTests {
    @Test
    func `track span matches last inclusive frame`() {
        #expect(timelineTrackFrameSpan(150) == 149)
        #expect(timelineLastInclusiveFrame(150) == 149)
        #expect(timelineTrackFrameSpan(1) == 0)
        #expect(timelineTrackFrameSpan(0) == 0)
    }

    @Test
    func `scrub at track end selects last inclusive frame`() {
        let duration: Int64 = 150
        let pointsPerFrame: CGFloat = 10
        let visibleX = trackLeadingInset + CGFloat(timelineTrackFrameSpan(duration)) * pointsPerFrame
        let frame = timelineFrame(atVisibleX: visibleX,
                                  pointsPerFrame: pointsPerFrame,
                                  scrollX: 0,
                                  duration: duration)
        #expect(frame == duration - 1)
    }

    @Test
    func `scrub past track end still clamps to last inclusive frame`() {
        let duration: Int64 = 150
        let pointsPerFrame: CGFloat = 10
        let visibleX = trackLeadingInset + CGFloat(duration + 5) * pointsPerFrame
        let frame = timelineFrame(atVisibleX: visibleX,
                                  pointsPerFrame: pointsPerFrame,
                                  scrollX: 0,
                                  duration: duration)
        #expect(frame == duration - 1)
    }

    @Test
    func `evaluation clamps duration marker to last inclusive frame`() {
        #expect(timelineEvaluationFrame(150, duration: 150) == 149)
        #expect(timelineEvaluationFrame(149, duration: 150) == 149)
        #expect(timelineEvaluationTime(150, duration: 150) == 149)
    }

    @Test
    func `display x for duration marker matches last inclusive frame`() {
        let duration: Int64 = 150
        let pointsPerFrame: CGFloat = 6
        let last = timelineEvaluationFrame(duration, duration: duration)
        let staleX = timelineX(for: duration, pointsPerFrame: pointsPerFrame)
        let displayX = timelineX(for: last, pointsPerFrame: pointsPerFrame)
        #expect(last == duration - 1)
        #expect(displayX == CGFloat(duration - 1) * pointsPerFrame)
        #expect(staleX != displayX)
    }

    @Test
    func `zero duration clamps to frame zero`() {
        let frame = timelineFrame(atVisibleX: trackLeadingInset + 100,
                                  pointsPerFrame: 10,
                                  scrollX: 0,
                                  duration: 0)
        #expect(frame == 0)
        #expect(timelineEvaluationFrame(0, duration: 0) == 0)
        #expect(timelineLastInclusiveFrame(0) == 0)
    }

    @Test
    func `visible keyframe indexes include intersecting segments`() {
        let indexes = timelineVisibleKeyframeIndexes(frames: [0, 10, 20, 30, 40],
                                                     pointsPerFrame: 10,
                                                     scrollX: 100,
                                                     viewportWidth: 100,
                                                     prefetchWidth: 0)
        #expect(indexes.diamonds == 1 ..< 2)
        #expect(indexes.segments == 0 ..< 2)
    }

    @Test
    func `visible keyframe indexes clamp to available items`() {
        let indexes = timelineVisibleKeyframeIndexes(frames: [0, 10, 20],
                                                     pointsPerFrame: 10,
                                                     scrollX: 0,
                                                     viewportWidth: 100,
                                                     prefetchWidth: 0)
        #expect(indexes.diamonds == 0 ..< 1)
        #expect(indexes.segments == 0 ..< 1)
    }

    @Test
    func `visible keyframe indexes retain segment crossing viewport`() {
        let indexes = timelineVisibleKeyframeIndexes(frames: [0, 100],
                                                     pointsPerFrame: 10,
                                                     scrollX: 400,
                                                     viewportWidth: 100,
                                                     prefetchWidth: 0)
        #expect(indexes.diamonds.isEmpty)
        #expect(indexes.segments == 0 ..< 1)
    }

    @Test
    func `visible keyframe indexes are empty for invalid viewport`() {
        let indexes = timelineVisibleKeyframeIndexes(frames: [0, 10],
                                                     pointsPerFrame: 10,
                                                     scrollX: 0,
                                                     viewportWidth: 0)
        #expect(indexes.diamonds.isEmpty)
        #expect(indexes.segments.isEmpty)
    }

    @Test
    func `scroll anchor follows surviving row identity`() {
        let anchor = TimelineScrollAnchor(rowID: .layer(2), rowIndex: 1, offsetY: 7)
        let rows = [
            TimelineRow(id: .layer(2), layerID: 2, kind: .layer),
            TimelineRow(id: .layer(1), layerID: 1, kind: .layer),
        ]
        #expect(timelineScrollAnchorTargetIndex(anchor, rows: rows) == 0)
    }

    @Test
    func `scroll anchor falls back to nearest row index when removed`() {
        let anchor = TimelineScrollAnchor(rowID: .keyframeTrack(1, "transform.position"),
                                          rowIndex: 2,
                                          offsetY: 5)
        let rows = [
            TimelineRow(id: .layer(1), layerID: 1, kind: .layer),
            TimelineRow(id: .layer(2), layerID: 2, kind: .layer),
        ]
        #expect(timelineScrollAnchorTargetIndex(anchor, rows: rows) == 1)
    }
}

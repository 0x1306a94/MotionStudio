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
}

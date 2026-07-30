//
//  TimelineScrollCoordinator.swift
//  MotionStudioApp
//
//  Horizontal scroll / zoom metrics shared by timeline chrome.
//

import UIKit

@MainActor
final class TimelineScrollCoordinator {
    private let editorState: EditorState
    private let playheadClock: PlayheadClock

    var duration: Int64 = 0
    var layerColumnWidth: CGFloat = minLayerColumnWidth
    private(set) var trackViewportWidth: CGFloat = 1
    private var lastResolvedPointsPerFrame: CGFloat = pixelsPerFrame

    init(editorState: EditorState, playheadClock: PlayheadClock) {
        self.editorState = editorState
        self.playheadClock = playheadClock
    }

    var pointsPerFrame: CGFloat {
        let totalFrames = CGFloat(max(timelineTrackFrameSpan(duration), 1))
        let fitPointsPerFrame = max(minTimelinePointsPerFrame, trackViewportWidth / totalFrames)
        return min(max(CGFloat(editorState.timelinePointsPerFrame), fitPointsPerFrame), maxTimelinePointsPerFrame)
    }

    var trackWidth: CGFloat {
        max(CGFloat(timelineTrackFrameSpan(duration)) * pointsPerFrame, trackViewportWidth)
    }

    var scrollX: CGFloat {
        min(max(CGFloat(editorState.timelineScrollX), 0), max(0, trackWidth - trackViewportWidth))
    }

    var contentViewportWidth: CGFloat {
        trackLeadingInset * 2 + trackViewportWidth
    }

    func updateTrackViewportWidth(_ width: CGFloat) {
        trackViewportWidth = max(1, width)
        clampScroll()
        lastResolvedPointsPerFrame = pointsPerFrame
    }

    func zoomBy(_ factor: CGFloat) {
        let next = CGFloat(editorState.timelinePointsPerFrame) * factor
        setTimelinePointsPerFrame(Double(min(max(next, minTimelinePointsPerFrame), maxTimelinePointsPerFrame)))
    }

    func setTimelinePointsPerFrame(_ value: Double) {
        let oldPoints = pointsPerFrame
        editorState.timelinePointsPerFrame = value
        let newPoints = pointsPerFrame
        preservePlayheadDuringZoom(from: oldPoints, to: newPoints)
        lastResolvedPointsPerFrame = newPoints
    }

    func noteExternalZoomChange() {
        let newPoints = pointsPerFrame
        preservePlayheadDuringZoom(from: lastResolvedPointsPerFrame, to: newPoints)
        lastResolvedPointsPerFrame = newPoints
    }

    func clampScroll() {
        let clamped = min(max(CGFloat(editorState.timelineScrollX), 0), max(0, trackWidth - trackViewportWidth))
        if CGFloat(editorState.timelineScrollX) != clamped {
            editorState.timelineScrollX = Double(clamped)
        }
    }

    func visibleContentX(for frame: Int64) -> CGFloat {
        let displayFrame = timelineEvaluationFrame(frame, duration: duration)
        return trackLeadingInset + timelineX(for: displayFrame, pointsPerFrame: pointsPerFrame) - scrollX
    }

    func frame(atVisibleX visibleX: CGFloat) -> Int64 {
        timelineFrame(atVisibleX: visibleX,
                      pointsPerFrame: pointsPerFrame,
                      scrollX: scrollX,
                      duration: duration)
    }

    private func preservePlayheadDuringZoom(from oldPointsPerFrame: CGFloat, to newPointsPerFrame: CGFloat) {
        guard oldPointsPerFrame > 0 else {
            clampScroll()
            return
        }
        let oldVisibleX = timelineX(for: playheadClock.frame, pointsPerFrame: oldPointsPerFrame)
            - CGFloat(editorState.timelineScrollX)
        guard oldVisibleX >= 0, oldVisibleX <= trackViewportWidth else {
            clampScroll()
            return
        }
        let nextScrollX = timelineX(for: playheadClock.frame, pointsPerFrame: newPointsPerFrame) - oldVisibleX
        let nextTrackWidth = max(CGFloat(timelineTrackFrameSpan(duration)) * newPointsPerFrame, trackViewportWidth)
        editorState.timelineScrollX = Double(min(max(nextScrollX, 0),
                                                 max(0, nextTrackWidth - trackViewportWidth)))
    }
}

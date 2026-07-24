//
//  TimeRangeTrackView.swift
//  MotionStudioApp
//

import SwiftUI

struct TimeRangeTrackView: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isSelected: Bool
    @Binding var isTimeRangeDragging: Bool
    let registerEdit: (String) -> Void
    @State private var dragStartRange: TimeRangeDraft?
    @State private var dragFrameOffset: Int64 = 0
    @State private var didDragRange = false

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let paths = timelineAnimatedPropertyPaths(core: core, layerID: layerID)
        let range = keyframeRange(paths: paths)
        let barHeight = layerRowHeight - 8
        let centerY = layerRowHeight / 2

        ZStack(alignment: .topLeading) {
            if let range {
                let startX = trackLeadingInset + timelineX(for: range.startFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let endX = trackLeadingInset + timelineX(for: range.endFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let spanWidth = max(endX - startX, 2)
                TimeRangeBarBody(isSelected: isSelected)
                    .frame(width: spanWidth, height: barHeight)
                    .contentShape(RoundedRectangle(cornerRadius: 4))
                    .onTapGesture(perform: selectLayer)
                    .overlay(alignment: .leading) {
                        dragHandle(edge: .leading, range: range, paths: paths)
                    }
                    .overlay(alignment: .trailing) {
                        dragHandle(edge: .trailing, range: range, paths: paths)
                    }
                    .position(x: startX + spanWidth / 2, y: centerY)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func dragHandle(edge: TimeRangeDragEdge, range: TimeRangeDraft, paths: [String]) -> some View {
        Rectangle()
            .fill(Color.clear)
            .frame(width: 28, height: layerRowHeight)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 3, coordinateSpace: .named("timelineViewport"))
                    .onChanged { value in
                        isTimeRangeDragging = true
                        updateRangeDrag(edge: edge, range: range, paths: paths, locationX: value.location.x)
                    }
                    .onEnded { _ in
                        endRangeDrag()
                    },
            )
    }

    private func updateRangeDrag(edge: TimeRangeDragEdge, range: TimeRangeDraft, paths: [String], locationX: CGFloat) {
        let pointerFrame = timelineFrame(at: locationX)
        if dragStartRange == nil {
            dragStartRange = range
            dragFrameOffset = pointerFrame - range.frame(for: edge)
            didDragRange = false
            selectLayer()
        }
        guard let startRange = dragStartRange,
              let currentRange = keyframeRange(paths: paths)
        else {
            return
        }
        let sourceFrame: Int64
        let targetFrame: Int64
        let draggedFrame = pointerFrame - dragFrameOffset
        switch edge {
        case .leading:
            sourceFrame = currentRange.startFrame
            targetFrame = min(max(draggedFrame, 0), startRange.leadingMaxFrame)
        case .trailing:
            sourceFrame = currentRange.endFrame
            targetFrame = min(max(draggedFrame, startRange.trailingMinFrame), duration)
        }
        guard sourceFrame != targetFrame else { return }
        moveEndpointKeyframes(paths: paths, from: sourceFrame, to: targetFrame)
        didDragRange = true
    }

    private func endRangeDrag() {
        dragStartRange = nil
        dragFrameOffset = 0
        isTimeRangeDragging = false
        core.endDrag()
        if didDragRange {
            registerEdit("Move Time Range")
        }
        didDragRange = false
    }

    private func selectLayer() {
        editorState.selectedLayerID = layerID
        editorState.selectedTimelineProperty = nil
        editorState.selectedTimelineSegment = nil
    }

    private func keyframeRange(paths: [String]) -> TimeRangeDraft? {
        let frames = paths
            .flatMap { core.keyframes(entityID: layerID, path: $0).map(\.frame) }
        let uniqueFrames = Array(Set(frames)).sorted()
        guard let firstFrame = uniqueFrames.first,
              let lastFrame = uniqueFrames.last,
              firstFrame < lastFrame
        else {
            return nil
        }
        let leadingMaxFrame = uniqueFrames.dropFirst().first.map { $0 - 1 } ?? lastFrame - 1
        let trailingMinFrame = uniqueFrames.dropLast().last.map { $0 + 1 } ?? firstFrame + 1
        return TimeRangeDraft(startFrame: firstFrame,
                              endFrame: lastFrame,
                              leadingMaxFrame: leadingMaxFrame,
                              trailingMinFrame: trailingMinFrame)
    }

    private func moveEndpointKeyframes(paths: [String], from sourceFrame: Int64, to targetFrame: Int64) {
        for path in paths where core.keyframes(entityID: layerID, path: path).contains(where: { $0.frame == sourceFrame }) {
            core.moveKeyframe(entityID: layerID, path: path, from: sourceFrame, to: targetFrame)
        }
    }

    private func timelineFrame(at visibleX: CGFloat) -> Int64 {
        Int64(((visibleX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
    }
}

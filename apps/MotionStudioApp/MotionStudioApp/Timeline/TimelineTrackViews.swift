//
//  TimelineTrackViews.swift
//  MotionStudioApp
//
//  Time graph rows, clip bars, keyframe lanes, and ruler drawing.
//

import SwiftUI

struct TrackRow: View {
    let core: MotionDocumentCore
    let row: TimelineRow
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        switch row.kind {
        case .layer:
            TimeRangeTrackView(core: core, layerID: row.layerID,
                               pointsPerFrame: pointsPerFrame,
                               scrollX: scrollX,
                               isSelected: editorState.selectedLayerID == row.layerID,
                               editorState: editorState)

        case let .propertySpan(path, label):
            PropertyTrackView(core: core,
                              layerID: row.layerID,
                              path: path,
                              label: label,
                              pointsPerFrame: pointsPerFrame,
                              scrollX: scrollX,
                              isSelected: isLayerSelected || editorState.selectedTimelineProperty == TimelinePropertySelection(layerID: row.layerID, path: path),
                              editorState: editorState)

        case let .keyframeTrack(path, _):
            ManualKeyframeTrackView(core: core,
                                    layerID: row.layerID,
                                    path: path,
                                    duration: duration,
                                    pointsPerFrame: pointsPerFrame,
                                    scrollX: scrollX,
                                    isTrackSelected: isLayerSelected,
                                    editorState: editorState,
                                    perform: perform,
                                    registerEdit: registerEdit)
        }
    }

    private var isLayerSelected: Bool {
        editorState.selectedLayerID == row.layerID
    }
}

private struct TimeRangeTrackView: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isSelected: Bool
    let editorState: EditorState

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let frames = timelineAnimatedPropertyPaths(core: core, layerID: layerID)
            .flatMap { core.keyframes(entityID: layerID, path: $0).map(\.frame) }
        let firstFrame = frames.min()
        let lastFrame = frames.max()
        let barHeight = layerRowHeight - 8
        let centerY = layerRowHeight / 2

        ZStack(alignment: .topLeading) {
            if let firstFrame, let lastFrame, firstFrame < lastFrame {
                let startX = trackLeadingInset + timelineX(for: firstFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let endX = trackLeadingInset + timelineX(for: lastFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let spanWidth = max(endX - startX, 2)
                TimeRangeBarBody(isSelected: isSelected)
                    .frame(width: spanWidth, height: barHeight)
                    .contentShape(RoundedRectangle(cornerRadius: 4))
                    .onTapGesture {
                        editorState.selectedLayerID = layerID
                        editorState.selectedTimelineProperty = nil
                        editorState.selectedTimelineSegment = nil
                    }
                    .position(x: startX + spanWidth / 2, y: centerY)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

private struct PropertyTrackView: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let path: String
    let label: String
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isSelected: Bool
    let editorState: EditorState

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let frames = core.keyframes(entityID: layerID, path: path).map(\.frame)
        let firstFrame = frames.min()
        let lastFrame = frames.max()

        ZStack(alignment: .topLeading) {
            if let firstFrame, let lastFrame, firstFrame < lastFrame {
                let startX = trackLeadingInset + timelineX(for: firstFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let endX = trackLeadingInset + timelineX(for: lastFrame, pointsPerFrame: pointsPerFrame) - scrollX
                let spanWidth = max(endX - startX, 2)
                PropertyTrackBar(label: label, isSelected: isSelected)
                    .frame(width: spanWidth, height: propertyRowHeight - 8)
                    .position(x: startX + spanWidth / 2, y: propertyRowHeight / 2)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .contentShape(Rectangle())
        .onTapGesture {
            editorState.selectedLayerID = layerID
            editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: layerID,
                                                                             path: path)
            editorState.selectedTimelineSegment = nil
        }
    }
}

private struct TimeRangeBarBody: View {
    let isSelected: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: 4)
            .fill(isSelected ? Color.accentColor.opacity(0.9) : Color.secondary.opacity(0.08))
            .overlay(alignment: .leading) {
                timeRangeEndpointHandle
            }
            .overlay(alignment: .trailing) {
                timeRangeEndpointHandle
            }
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? Color.white.opacity(0.65) : Color.secondary.opacity(0.14),
                            lineWidth: 1),
            )
    }

    private var timeRangeEndpointHandle: some View {
        RoundedRectangle(cornerRadius: 1.5)
            .fill(isSelected ? Color.white.opacity(0.9) : Color.secondary.opacity(0.28))
            .frame(width: 2, height: layerRowHeight - 14)
            .padding(.horizontal, 6)
    }
}

private struct PropertyTrackBar: View {
    let label: String
    let isSelected: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: 4)
            .fill(isSelected ? Color.accentColor.opacity(0.9) : Color.clear)
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? Color.white.opacity(0.7) : Color.secondary.opacity(0.35),
                            lineWidth: 1),
            )
            .overlay(alignment: .leading) {
                propertyEndpointHandle
            }
            .overlay(alignment: .trailing) {
                propertyEndpointHandle
            }
            .overlay {
                Text(label)
                    .font(.system(size: 9))
                    .fontWeight(.semibold)
                    .foregroundStyle(isSelected ? .white.opacity(0.92) : .secondary)
                    .lineLimit(1)
                    .padding(.horizontal, 4)
            }
    }

    private var propertyEndpointHandle: some View {
        RoundedRectangle(cornerRadius: 1.5)
            .fill(isSelected ? Color.white.opacity(0.9) : Color.secondary.opacity(0.35))
            .frame(width: 2, height: 9)
            .padding(.horizontal, 5)
    }
}

private struct ManualKeyframeTrackView: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let path: String
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isTrackSelected: Bool
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let keyframes = core.keyframes(entityID: layerID, path: path).sorted { $0.frame < $1.frame }
        let segments = zip(keyframes, keyframes.dropFirst()).map(KeyframeSegment.init)

        ZStack(alignment: .topLeading) {
            Color.clear
            ForEach(segments) { segment in
                KeyframeConnectionSegment(layerID: layerID,
                                          path: path,
                                          segment: segment,
                                          pointsPerFrame: pointsPerFrame,
                                          scrollX: scrollX,
                                          isTrackSelected: isTrackSelected,
                                          editorState: editorState)
            }
            ForEach(keyframes) { keyframe in
                KeyframeDiamond(keyframe: keyframe,
                                duration: duration,
                                pointsPerFrame: pointsPerFrame,
                                scrollX: scrollX,
                                isSelected: isKeyframeSelected(keyframe.frame))
                { from, to in
                    core.moveKeyframe(entityID: layerID, path: path, from: from, to: to)
                } onMoveEnded: {
                    core.endDrag()
                    registerEdit("Move Keyframe")
                } onDelete: {
                    perform("Delete Keyframe") {
                        core.removeKeyframe(entityID: layerID, path: path, frame: keyframe.frame)
                    }
                } onSetEasing: { easing in
                    perform("Set Easing") {
                        core.setEasing(entityID: layerID, path: path,
                                       frame: keyframe.frame, easing: easing)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func isKeyframeSelected(_ frame: Int64) -> Bool {
        if isTrackSelected {
            return true
        }
        guard let selection = editorState.selectedTimelineSegment,
              selection.layerID == layerID,
              selection.path == path
        else {
            return false
        }
        return frame == selection.startFrame || frame == selection.endFrame
    }
}

private struct KeyframeSegment: Identifiable {
    let start: KeyframeInfo
    let end: KeyframeInfo

    var id: String {
        "\(start.frame)-\(end.frame)"
    }
}

private struct KeyframeConnectionSegment: View {
    let layerID: UInt64
    let path: String
    let segment: KeyframeSegment
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isTrackSelected: Bool
    let editorState: EditorState
    @State private var isHovering = false

    var body: some View {
        let startX = trackLeadingInset + timelineX(for: segment.start.frame, pointsPerFrame: pointsPerFrame) - scrollX
        let endX = trackLeadingInset + timelineX(for: segment.end.frame, pointsPerFrame: pointsPerFrame) - scrollX
        let width = max(endX - startX, 2)
        let centerY = propertyRowHeight / 2
        let selected = isSelected || isTrackSelected

        ZStack {
            Capsule()
                .fill(selected ? Color.accentColor : Color.secondary.opacity(isHovering ? 0.34 : 0.22))
                .frame(height: selected ? 4 : (isHovering ? 3 : 2))
            if isSelected || isHovering {
                EasingSegmentBadge(easing: segment.start.easing, isSelected: selected)
            }
        }
        .frame(width: width, height: propertyRowHeight)
        .contentShape(Rectangle())
        .onHover { isHovering = $0 }
        .onTapGesture {
            editorState.selectedLayerID = layerID
            editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: layerID,
                                                                             path: path)
            editorState.selectedTimelineSegment = TimelineSegmentSelection(layerID: layerID,
                                                                           path: path,
                                                                           startFrame: segment.start.frame,
                                                                           endFrame: segment.end.frame)
        }
        .position(x: startX + width / 2, y: centerY)
    }

    private var isSelected: Bool {
        editorState.selectedTimelineSegment == TimelineSegmentSelection(layerID: layerID,
                                                                        path: path,
                                                                        startFrame: segment.start.frame,
                                                                        endFrame: segment.end.frame)
    }
}

private struct EasingSegmentBadge: View {
    let easing: EasingInfo
    let isSelected: Bool

    var body: some View {
        Image(systemName: easing.kind == .hold ? "pause.fill" : "point.topleft.down.curvedto.point.bottomright.up")
            .font(.system(size: 9, weight: .semibold))
            .foregroundStyle(isSelected ? .white : Color.secondary.opacity(0.7))
            .frame(width: 18, height: 16)
            .background(isSelected ? Color.accentColor : Color.secondary.opacity(0.08),
                        in: RoundedRectangle(cornerRadius: 5))
            .overlay(
                RoundedRectangle(cornerRadius: 5)
                    .stroke(isSelected ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(0.22),
                            lineWidth: 1),
            )
    }
}

struct RulerCanvas: View {
    let duration: Int64
    let frameRate: Double
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let contentInset: CGFloat

    var body: some View {
        Canvas { context, size in
            let total = Int(duration)
            let second = max(Int(frameRate.rounded()), 1)
            let majorStep = rulerMajorStep(pointsPerFrame: pointsPerFrame)
            let minorStep = max(majorStep / 5, 1)
            let visibleTrackWidth = max(0, size.width - contentInset)
            let firstFrame = max(0, Int(floor(scrollX / pointsPerFrame)))
            let lastFrame = min(total, Int(ceil((scrollX + visibleTrackWidth) / pointsPerFrame)) + minorStep)
            let firstTick = firstFrame + (minorStep - firstFrame % minorStep) % minorStep
            for frame in stride(from: firstTick, through: lastFrame, by: minorStep) {
                let x = contentInset + timelineX(for: Int64(frame), pointsPerFrame: pointsPerFrame) - scrollX
                guard x >= contentInset, x <= size.width else { continue }
                let isMajor = frame % majorStep == 0
                var tick = Path()
                tick.move(to: CGPoint(x: x, y: isMajor ? 8 : 15))
                tick.addLine(to: CGPoint(x: x, y: 22))
                context.stroke(tick, with: .color(.secondary), lineWidth: isMajor ? 1 : 0.5)
                if isMajor {
                    context.draw(Text(rulerLabel(frame: frame, second: second)).font(.system(size: 9)),
                                 at: CGPoint(x: x + 8, y: 8), anchor: .leading)
                }
            }
        }
    }

    private func rulerMajorStep(pointsPerFrame: CGFloat) -> Int {
        let targetFrames = max(1, Int((80 / max(pointsPerFrame, 1)).rounded()))
        var scale = 1
        while scale * 10 < targetFrames {
            scale *= 10
        }
        for multiplier in [1, 2, 5, 10] {
            let step = multiplier * scale
            if step >= targetFrames {
                return step
            }
        }
        return 10 * scale
    }

    private func rulerLabel(frame: Int, second: Int) -> String {
        if frame % second == 0 {
            return "\(frame / second)s"
        }
        return "\(frame)f"
    }
}

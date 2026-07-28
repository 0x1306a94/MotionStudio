//
//  KeyframeConnectionSegment.swift
//  MotionStudioApp
//

import SwiftUI

struct KeyframeConnectionSegment: View {
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let path: String
    let segment: KeyframeSegment
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isTrackSelected: Bool
    let onSetEasing: (EasingInfo) -> Void
    let onEasingCommit: () -> Void
    let onEasingDragBegan: () -> Void
    let onEasingDragEnded: () -> Void

    @State private var isHovering = false
    @State private var showEasingPopover = false

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
            EasingSegmentBadge(easing: segment.start.easing, isSelected: selected || showEasingPopover)
                .onTapGesture {
                    selectSegment()
                    showEasingPopover = true
                }
                .popover(isPresented: $showEasingPopover, arrowEdge: .bottom) {
                    KeyframeEasingPopover(easing: segment.start.easing,
                                          easingAffectsPlayback: true,
                                          onSetEasing: onSetEasing,
                                          onDelete: nil,
                                          onCommit: onEasingCommit,
                                          onDragBegan: onEasingDragBegan,
                                          onDragEnded: onEasingDragEnded)
                }
        }
        .frame(width: width, height: propertyRowHeight)
        .contentShape(Rectangle())
        .onHover { isHovering = $0 }
        .onTapGesture {
            selectSegment()
        }
        .position(x: startX + width / 2, y: centerY)
    }

    private var isSelected: Bool {
        editorState.selectedTimelineSegment == TimelineSegmentSelection(layerID: layerID,
                                                                        path: path,
                                                                        startFrame: segment.start.frame,
                                                                        endFrame: segment.end.frame)
    }

    private func selectSegment() {
        editorState.selectedLayerID = layerID
        editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: layerID,
                                                                         path: path)
        editorState.selectedTimelineSegment = TimelineSegmentSelection(layerID: layerID,
                                                                       path: path,
                                                                       startFrame: segment.start.frame,
                                                                       endFrame: segment.end.frame)
    }
}

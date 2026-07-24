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

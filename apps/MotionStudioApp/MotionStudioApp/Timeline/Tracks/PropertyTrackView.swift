//
//  PropertyTrackView.swift
//  MotionStudioApp
//

import SwiftUI

struct PropertyTrackView: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let path: String
    let label: String
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isSelected: Bool

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

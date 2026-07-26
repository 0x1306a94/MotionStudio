//
//  TrackRow.swift
//  MotionStudioApp
//

import SwiftUI

struct TrackRow: View {
    @Environment(EditorState.self) private var editorState

    let row: TimelineRow
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    @Binding var isTimeRangeDragging: Bool
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        switch row.kind {
        case .layer:
            TimeRangeTrackView(layerID: row.layerID,
                               duration: duration,
                               pointsPerFrame: pointsPerFrame,
                               scrollX: scrollX,
                               isSelected: isLayerSelected,
                               isTimeRangeDragging: $isTimeRangeDragging,
                               registerEdit: registerEdit)

        case let .propertySpan(path, label):
            PropertyTrackView(layerID: row.layerID,
                              path: path,
                              label: label,
                              pointsPerFrame: pointsPerFrame,
                              scrollX: scrollX,
                              isSelected: isLayerSelected || editorState.selectedTimelineProperty == TimelinePropertySelection(layerID: row.layerID, path: path))

        case let .keyframeTrack(path, _):
            ManualKeyframeTrackView(layerID: row.layerID,
                                    path: path,
                                    duration: duration,
                                    pointsPerFrame: pointsPerFrame,
                                    scrollX: scrollX,
                                    isTrackSelected: isLayerSelected,
                                    perform: perform,
                                    registerEdit: registerEdit)
        }
    }

    private var isLayerSelected: Bool {
        editorState.isLayerSelected(row.layerID)
    }
}

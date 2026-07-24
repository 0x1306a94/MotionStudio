//
//  PropertySubRow.swift
//  MotionStudioApp
//

import SwiftUI

struct PropertySubRow: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let label: String
    let path: String

    var body: some View {
        let hasKeyframe = core.keyframes(entityID: layerID, path: path)
            .contains { $0.frame == editorState.playheadFrame }
        HStack(spacing: 4) {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer()
            if hasKeyframe {
                Image(systemName: "diamond.fill")
                    .font(.system(size: 8))
                    .foregroundStyle(.yellow)
            }
        }
        .font(.caption)
        .padding(.leading, 28)
        .padding(.trailing, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())
        .background(isSelected ? Color.accentColor.opacity(0.08) : Color.clear)
        .onTapGesture {
            editorState.selectedLayerID = layerID
            editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: layerID, path: path)
            editorState.selectedTimelineSegment = nil
        }
    }

    private var isSelected: Bool {
        editorState.selectedTimelineProperty == TimelinePropertySelection(layerID: layerID, path: path)
    }
}

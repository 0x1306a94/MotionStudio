//
//  TimelineLayerColumn.swift
//  MotionStudioApp
//
//  Layer tree rows for the timeline's frozen left column.
//

import SwiftUI

struct LayerColumnHeader: View {
    var body: some View {
        HStack {
            Text("Layers")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 8)
    }
}

struct LayerColumn: View {
    let core: MotionDocumentCore
    let rows: [TimelineRow]
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let clearSelection: () -> Void

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture(perform: clearSelection)
            VStack(spacing: 0) {
                ForEach(rows) { row in
                    switch row.kind {
                    case .layer:
                        LayerRow(core: core, layerID: row.layerID,
                                 editorState: editorState, perform: perform)
                            .frame(height: row.height)
                    case let .propertySpan(path, label), let .keyframeTrack(path, label):
                        PropertySubRow(core: core, layerID: row.layerID,
                                       label: label, path: path,
                                       editorState: editorState)
                            .frame(height: row.height)
                    }
                }
            }
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
    }
}

private struct LayerRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let selected = editorState.selectedLayerID == layerID
        let visible = core.layerIsVisible(layerID)
        let locked = core.layerIsLocked(layerID)
        HStack(spacing: 6) {
            Image(systemName: layerSymbol(core.layerType(layerID)))
                .foregroundStyle(.secondary)
            Text(core.layerName(layerID))
                .lineLimit(1)
            Spacer()
            Button {
                perform(visible ? "Hide Layer" : "Show Layer") {
                    core.setLayerVisible(layerID, visible: !visible)
                }
            } label: {
                Image(systemName: visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(.secondary)
                    .font(.system(size: layerActionIconSize))
                    .frame(width: layerActionIconSize, height: layerActionIconSize)
            }
            .frame(width: layerActionButtonSize, height: layerActionButtonSize)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            Button {
                perform(locked ? "Unlock Layer" : "Lock Layer") {
                    core.setLayerLocked(layerID, locked: !locked)
                }
            } label: {
                Image(systemName: locked ? "lock.fill" : "lock.open")
                    .foregroundStyle(.secondary)
                    .font(.system(size: layerActionIconSize))
                    .frame(width: layerActionIconSize, height: layerActionIconSize)
            }
            .frame(width: layerActionButtonSize, height: layerActionButtonSize)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
        }
        .font(.callout)
        .padding(.horizontal, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(selected ? Color.accentColor.opacity(0.25) : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture {
            editorState.selectedLayerID = layerID
            editorState.selectedTimelineProperty = nil
            editorState.selectedTimelineSegment = nil
        }
    }
}

private struct PropertySubRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let label: String
    let path: String
    let editorState: EditorState

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

private func layerSymbol(_ type: Int32) -> String {
    switch type {
    case 1:
        return "photo"
    case 2:
        return "textformat"
    case 3:
        return "circle.dashed"
    case 4:
        return "film"
    default:
        return "square"
    }
}

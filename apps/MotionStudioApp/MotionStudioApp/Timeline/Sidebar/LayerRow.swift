//
//  LayerRow.swift
//  MotionStudioApp
//

import MotionStudioBridging
import SwiftUI

struct LayerRow: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let perform: (String, () -> Void) -> Void
    let arrange: (UInt64, LayerArrangeAction) -> Void
    /// Reports finger Y in the timeline viewport coordinate space.
    let onReorderDragChanged: (UInt64, CGFloat) -> Void
    let onReorderDragEnded: () -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let selected = editorState.isLayerSelected(layerID)
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
            editorState.selectLayer(layerID, additive: KeyboardModifiers.shiftPressed)
        }
        .gesture(
            DragGesture(minimumDistance: 4,
                        coordinateSpace: .named(timelineLayerListViewportCoordinateSpace))
                .onChanged { value in
                    onReorderDragChanged(layerID, value.location.y)
                }
                .onEnded { _ in
                    onReorderDragEnded()
                },
        )
        .contextMenu {
            Button("Bring to Front") { arrange(layerID, .bringToFront) }
            Button("Bring Forward") { arrange(layerID, .bringForward) }
            Button("Send Backward") { arrange(layerID, .sendBackward) }
            Button("Send to Back") { arrange(layerID, .sendToBack) }
        }
    }

    private func layerSymbol(_ type: MS_LAYER) -> String {
        switch type {
        case .IMAGE:
            "photo"
        case .TEXT:
            "textformat"
        case .GROUP:
            "circle.dashed"
        case .PRECOMP:
            "film"
        default:
            "square"
        }
    }
}

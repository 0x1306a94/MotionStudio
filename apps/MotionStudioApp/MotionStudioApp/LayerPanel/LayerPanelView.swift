//
//  LayerPanelView.swift
//  MotionStudioApp
//
//  Layer list with selection and shape creation.
//

import SwiftUI

struct LayerPanelView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        @Bindable var editorState = editorState
        let core = document.core
        let compositionID = core.firstCompositionID
        // Topmost layer first in the UI (index 0 is the bottommost).
        let layerIDs = core.layerIDs(compositionID: compositionID).reversed()

        List(selection: $editorState.selectedLayerID) {
            ForEach(layerIDs, id: \.self) { layerID in
                LayerRow(id: layerID, name: core.layerName(layerID))
            }
        }
        .toolbar {
            ToolbarItemGroup {
                Button {
                    perform("Add Rectangle") {
                        let layerID = core.addRectLayer(compositionID: compositionID)
                        editorState.selectedLayerID = layerID
                    }
                } label: {
                    Label("Add Rectangle", systemImage: "rectangle.badge.plus")
                }
                Button {
                    perform("Add Ellipse") {
                        let layerID = core.addEllipseLayer(compositionID: compositionID)
                        editorState.selectedLayerID = layerID
                    }
                } label: {
                    Label("Add Ellipse", systemImage: "circle.badge.plus")
                }
                Button(role: .destructive) {
                    guard let selected = editorState.selectedLayerID else { return }
                    perform("Delete Layer") {
                        core.removeLayer(compositionID: compositionID, layerID: selected)
                    }
                    editorState.selectedLayerID = nil
                } label: {
                    Label("Delete Layer", systemImage: "trash")
                }
                .disabled(editorState.selectedLayerID == nil)
            }
        }
    }
}

private struct LayerRow: View {
    let id: UInt64
    let name: String

    var body: some View {
        Label(name, systemImage: "square.on.square")
            .tag(id)
    }
}

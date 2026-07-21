//
//  ProjectPanelView.swift
//  MotionStudioApp
//
//  Left panel: project assets (empty for now) plus the composition list and
//  the layer-creation toolbar. The layer stack itself lives in the timeline.
//

import SwiftUI

struct ProjectPanelView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let core = document.core
        let _ = core.revision
        let compositionID = core.firstCompositionID

        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Project")
                    .font(.headline)
                Spacer()
                Button {
                    perform("Add Rectangle") {
                        editorState.selectedLayerID = core.addRectLayer(compositionID: compositionID)
                    }
                } label: {
                    Image(systemName: "rectangle.badge.plus")
                }
                Button {
                    perform("Add Ellipse") {
                        editorState.selectedLayerID = core.addEllipseLayer(compositionID: compositionID)
                    }
                } label: {
                    Image(systemName: "circle.badge.plus")
                }
                Button(role: .destructive) {
                    guard let selected = editorState.selectedLayerID else { return }
                    perform("Delete Layer") {
                        core.removeLayer(compositionID: compositionID, layerID: selected)
                    }
                    editorState.selectedLayerID = nil
                } label: {
                    Image(systemName: "trash")
                }
                .disabled(editorState.selectedLayerID == nil)
            }
            .buttonStyle(.plain)
            .padding(8)

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                Text("Assets")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                ContentUnavailableView("No Assets",
                                       systemImage: "tray",
                                       description: Text("Import assets to use them here."))
                    .frame(maxHeight: 140)
            }
            .padding(8)

            Divider()

            List {
                Section("Compositions") {
                    ForEach(core.compositionIDs(), id: \.self) { id in
                        Label(core.compositionName(id), systemImage: "film")
                    }
                }
            }
            .listStyle(.sidebar)
        }
    }
}

//
//  ProjectPanelView.swift
//  MotionStudioApp
//
//  Left panel: project assets (empty for now) plus the composition list. The
//  layer stack itself lives in the timeline.
//

import SwiftUI

struct ProjectPanelView: View {
    let document: MotionDocument
    let clearSelection: () -> Void

    var body: some View {
        let core = document.core
        let _ = core.revision

        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Project")
                    .font(.headline)
                Spacer()
            }
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
        .contentShape(Rectangle())
        .onTapGesture(perform: clearSelection)
    }
}

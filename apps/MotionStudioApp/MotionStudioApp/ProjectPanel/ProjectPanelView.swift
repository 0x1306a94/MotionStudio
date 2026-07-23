//
//  ProjectPanelView.swift
//  MotionStudioApp
//
//  Left panel: project assets (empty for now) plus the composition list. The
//  layer stack itself lives in the timeline.
//

import SwiftUI

struct ProjectPanelView: View {
    let document: MotionProjectState
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

            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Compositions")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 8)
                        .padding(.top, 8)

                    ForEach(core.compositionIDs(), id: \.self) { id in
                        Button {
                            clearSelection()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "film")
                                    .frame(width: 16)
                                Text(core.compositionName(id))
                                    .lineLimit(1)
                                Spacer(minLength: 0)
                            }
                            .font(.subheadline)
                            .foregroundStyle(.primary)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 7)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .padding(.horizontal, 6)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture(perform: clearSelection)
    }
}

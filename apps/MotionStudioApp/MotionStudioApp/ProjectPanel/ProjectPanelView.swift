//
//  ProjectPanelView.swift
//  MotionStudioApp
//
//  Left panel: project assets plus the composition list. The layer stack
//  itself lives in the timeline.
//

import SwiftUI

struct ProjectPanelView: View {
    let document: MotionProjectState
    let clearSelection: () -> Void
    let importImage: () -> Void
    let importFont: () -> Void

    var body: some View {
        let core = document.core
        let _ = core.revision
        let assets = core.assetIDs()

        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Project")
                    .font(.headline)
                Spacer()
            }
            .padding(8)

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Assets")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Menu("Import") {
                        Button("Image", action: importImage)
                        Button("Font", action: importFont)
                    }
                    .font(.caption)
                    .buttonStyle(.borderless)
                }

                if assets.isEmpty {
                    ContentUnavailableView("No Assets",
                                           systemImage: "tray",
                                           description: Text("Import images or fonts to use them here."))
                        .frame(maxHeight: 140)
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 2) {
                            ForEach(assets, id: \.self) { assetID in
                                HStack(spacing: 8) {
                                    Image(systemName: core.isFontAsset(assetID) ? "textformat" : "photo")
                                        .frame(width: 16)
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text(core.assetName(assetID))
                                            .lineLimit(1)
                                        if core.isFontAsset(assetID) {
                                            Text("Font")
                                                .font(.caption2)
                                                .foregroundStyle(.secondary)
                                        } else {
                                            Text("\(core.assetWidth(assetID))×\(core.assetHeight(assetID))")
                                                .font(.caption2)
                                                .foregroundStyle(.secondary)
                                        }
                                    }
                                    Spacer(minLength: 0)
                                }
                                .font(.subheadline)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 6)
                            }
                        }
                    }
                    .frame(maxHeight: 180)
                }
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

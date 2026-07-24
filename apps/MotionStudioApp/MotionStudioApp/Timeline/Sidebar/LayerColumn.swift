//
//  LayerColumn.swift
//  MotionStudioApp
//

import SwiftUI

struct LayerColumn: View {
    let rows: [TimelineRow]
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
                        LayerRow(layerID: row.layerID, perform: perform)
                            .frame(height: row.height)
                    case let .propertySpan(path, label), let .keyframeTrack(path, label):
                        PropertySubRow(layerID: row.layerID,
                                       label: label,
                                       path: path)
                            .frame(height: row.height)
                    }
                }
            }
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
    }
}

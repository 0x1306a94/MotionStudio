//
//  TrackMatteInspector.swift
//  MotionStudioApp
//
//  Track matte type + source layer picker for the selected layer.
//

import MotionStudioBridging
import SwiftUI

struct TrackMatteInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let layerID: UInt64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let _ = core.panelRevision
        let type = currentType
        let sourceID = core.trackMatteLayerID(layerID: layerID)
        Text("Track Matte")
            .font(.subheadline)
            .foregroundStyle(.secondary)
        HStack(spacing: 8) {
            Picker("Type", selection: typeBinding) {
                ForEach(MS_TRACK_MATTE.allCases) { matteType in
                    Text(matteType.label).tag(matteType)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .fixedSize()
            .disabled(!isEditable)
            .id("track-matte-type-\(type.rawValue)-\(core.panelRevision)")

            Picker("Source", selection: sourceBinding) {
                Text("None").tag(UInt64(0))
                ForEach(candidateSourceIDs, id: \.self) { candidateID in
                    Text(core.layerName(candidateID)).tag(candidateID)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable || type == .NONE)
            .id("track-matte-source-\(sourceID)-\(core.panelRevision)")
        }
    }

    private var currentType: MS_TRACK_MATTE {
        core.trackMatteType(layerID: layerID)
    }

    private var candidateSourceIDs: [UInt64] {
        core.layerIDs(compositionID: compositionID).filter { $0 != layerID }
    }

    private var typeBinding: Binding<MS_TRACK_MATTE> {
        Binding {
            currentType
        } set: { newValue in
            guard isEditable else { return }
            let existingSource = core.trackMatteLayerID(layerID: layerID)
            let sourceID: UInt64 = if newValue == .NONE {
                0
            } else if existingSource != 0 {
                existingSource
            } else {
                candidateSourceIDs.first ?? 0
            }
            perform("Set Track Matte") {
                core.setTrackMatte(layerID: layerID, matteLayerID: sourceID, type: newValue)
            }
        }
    }

    private var sourceBinding: Binding<UInt64> {
        Binding {
            core.trackMatteLayerID(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let type = currentType == .NONE ? .ALPHA : currentType
            let resolvedType = newValue == 0 ? .NONE : type
            perform("Set Track Matte") {
                core.setTrackMatte(layerID: layerID, matteLayerID: newValue, type: resolvedType)
            }
        }
    }
}

//
//  TrackMatteInspector.swift
//  MotionStudioApp
//
//  Track matte type + source layer picker for the selected layer.
//

import SwiftUI

struct TrackMatteInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let layerID: UInt64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let _ = core.revision
        let type = currentType
        let sourceID = core.trackMatteLayerID(layerID: layerID)
        Text("Track Matte")
            .font(.subheadline)
            .foregroundStyle(.secondary)
        HStack(spacing: 8) {
            Picker("Type", selection: typeBinding) {
                ForEach(TrackMatteTypeTag.allCases) { matteType in
                    Text(matteType.label).tag(matteType)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .fixedSize()
            .disabled(!isEditable)
            .id("track-matte-type-\(type.rawValue)-\(core.revision)")

            Picker("Source", selection: sourceBinding) {
                Text("None").tag(UInt64(0))
                ForEach(candidateSourceIDs, id: \.self) { candidateID in
                    Text(core.layerName(candidateID)).tag(candidateID)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable || type == .none)
            .id("track-matte-source-\(sourceID)-\(core.revision)")
        }
    }

    private var currentType: TrackMatteTypeTag {
        TrackMatteTypeTag(rawValue: core.trackMatteType(layerID: layerID)) ?? .none
    }

    private var candidateSourceIDs: [UInt64] {
        core.layerIDs(compositionID: compositionID).filter { $0 != layerID }
    }

    private var typeBinding: Binding<TrackMatteTypeTag> {
        Binding {
            currentType
        } set: { newValue in
            guard isEditable else { return }
            let existingSource = core.trackMatteLayerID(layerID: layerID)
            let sourceID: UInt64 = if newValue == .none {
                0
            } else if existingSource != 0 {
                existingSource
            } else {
                candidateSourceIDs.first ?? 0
            }
            perform("Set Track Matte") {
                core.setTrackMatte(layerID: layerID, matteLayerID: sourceID, type: newValue.rawValue)
            }
        }
    }

    private var sourceBinding: Binding<UInt64> {
        Binding {
            core.trackMatteLayerID(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let type = currentType == .none ? TrackMatteTypeTag.alpha : currentType
            let resolvedType = newValue == 0 ? TrackMatteTypeTag.none : type
            perform("Set Track Matte") {
                core.setTrackMatte(layerID: layerID,
                                   matteLayerID: newValue,
                                   type: resolvedType.rawValue)
            }
        }
    }
}

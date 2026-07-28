//
//  FollowPathInspector.swift
//  MotionStudioApp
//
//  Follow Path constraint controls for the selected layer.
//

import MotionStudioBridging
import SwiftUI

struct FollowPathInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let layerID: UInt64
    let playheadFrame: Int64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    private let pathOffsetPath = "followPath.pathOffset"
    private let orientOffsetPath = "followPath.orientOffset"

    var body: some View {
        let _ = core.revision
        let enabled = core.followPathEnabled(layerID: layerID)
        let pathLayerID = core.followPathLayerID(layerID: layerID)
        let orient = core.followPathOrient(layerID: layerID)

        Text("Follow Path")
            .font(.subheadline)
            .foregroundStyle(.secondary)

        Toggle("Enabled", isOn: enabledBinding)
            .disabled(!isEditable)

        Picker("Path Layer", selection: pathLayerBinding) {
            Text("None").tag(UInt64(0))
            ForEach(candidatePathLayerIDs, id: \.self) { candidateID in
                Text(core.layerName(candidateID)).tag(candidateID)
            }
        }
        .disabled(!isEditable || !enabled)
        .id("follow-path-layer-\(pathLayerID)-\(core.revision)")

        Toggle("Orient Along Path", isOn: orientBinding)
            .disabled(!isEditable || !enabled)

        if enabled {
            floatRow(label: "Path Offset", path: pathOffsetPath)
            if orient {
                floatRow(label: "Orient Offset", path: orientOffsetPath)
            }
        }
    }

    private var candidatePathLayerIDs: [UInt64] {
        core.layerIDs(compositionID: compositionID).filter { candidateID in
            candidateID != layerID && core.layerType(candidateID) == .SHAPE
        }
    }

    private var enabledBinding: Binding<Bool> {
        Binding {
            core.followPathEnabled(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let existingPath = core.followPathLayerID(layerID: layerID)
            let pathID: UInt64 = if !newValue {
                0
            } else if existingPath != 0 {
                existingPath
            } else {
                candidatePathLayerIDs.first ?? 0
            }
            let orient = core.followPathOrient(layerID: layerID)
            perform("Set Follow Path") {
                core.setFollowPath(layerID: layerID, enabled: newValue && pathID != 0,
                                   pathLayerID: pathID, orientAlongPath: orient)
            }
        }
    }

    private var pathLayerBinding: Binding<UInt64> {
        Binding {
            core.followPathLayerID(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let orient = core.followPathOrient(layerID: layerID)
            perform("Set Follow Path") {
                core.setFollowPath(layerID: layerID, enabled: newValue != 0,
                                   pathLayerID: newValue, orientAlongPath: orient)
            }
        }
    }

    private var orientBinding: Binding<Bool> {
        Binding {
            core.followPathOrient(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let enabled = core.followPathEnabled(layerID: layerID)
            let pathID = core.followPathLayerID(layerID: layerID)
            perform("Set Follow Path") {
                core.setFollowPath(layerID: layerID, enabled: enabled,
                                   pathLayerID: pathID, orientAlongPath: newValue)
            }
        }
    }

    private func floatRow(label: String, path: String) -> some View {
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path)
            .contains(playheadFrame)
        return NumberPropertyRow(label: label,
                                 value: core.evaluateFloat(entityID: layerID, path: path,
                                                           frame: playheadFrame),
                                 hasKeyframeAtPlayhead: hasKeyframe,
                                 isEditable: isEditable)
        { newValue in
            guard isEditable else { return }
            perform("Set \(label)") {
                if hasKeyframe {
                    core.addKeyframeFloat(entityID: layerID, path: path,
                                          frame: playheadFrame, value: newValue)
                } else {
                    core.setStaticFloat(entityID: layerID, path: path, value: newValue)
                }
                core.endDrag()
            }
        } onToggleKeyframe: { value in
            guard isEditable else { return }
            if hasKeyframe {
                perform("Delete Keyframe") {
                    core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
                }
            } else {
                perform("Add Keyframe") {
                    core.addKeyframeFloat(entityID: layerID, path: path,
                                          frame: playheadFrame, value: value)
                }
            }
        }
    }
}

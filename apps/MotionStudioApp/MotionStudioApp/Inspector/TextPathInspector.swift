//
//  TextPathInspector.swift
//  MotionStudioApp
//
//  Text Path layout controls for the selected text layer.
//

import MotionStudioBridging
import SwiftUI

struct TextPathInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    let perform: (String, () -> Void) -> Void

    var body: some View {
        let _ = core.panelRevision
        let enabled = core.textPathEnabled(layerID: layerID)
        let pathLayerID = core.textPathLayerID(layerID: layerID)

        Text("Text Path")
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
        .id("text-path-layer-\(pathLayerID)-\(core.panelRevision)")

        Toggle("Reversed", isOn: reversedBinding)
            .disabled(!isEditable || !enabled)

        Toggle("Perpendicular", isOn: perpendicularBinding)
            .disabled(!isEditable || !enabled)

        Toggle("Force Alignment", isOn: forceAlignmentBinding)
            .disabled(!isEditable || !enabled)

        if enabled {
            floatRow(label: TextPathProperty.firstMargin.actionLabel,
                     path: TextPathProperty.firstMargin.path)
            floatRow(label: TextPathProperty.lastMargin.actionLabel,
                     path: TextPathProperty.lastMargin.path)
        }
    }

    private var candidatePathLayerIDs: [UInt64] {
        core.layerIDs(compositionID: compositionID).filter { candidateID in
            candidateID != layerID && core.layerType(candidateID) == .SHAPE
        }
    }

    private var enabledBinding: Binding<Bool> {
        Binding {
            core.textPathEnabled(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            let existingPath = core.textPathLayerID(layerID: layerID)
            let pathID: UInt64 = if !newValue {
                0
            } else if existingPath != 0 {
                existingPath
            } else {
                candidatePathLayerIDs.first ?? 0
            }
            perform("Set Text Path") {
                core.setTextPath(layerID: layerID,
                                 enabled: newValue && pathID != 0,
                                 pathLayerID: pathID,
                                 reversed: core.textPathReversed(layerID: layerID),
                                 perpendicular: core.textPathPerpendicular(layerID: layerID),
                                 forceAlignment: core.textPathForceAlignment(layerID: layerID))
            }
        }
    }

    private var pathLayerBinding: Binding<UInt64> {
        Binding {
            core.textPathLayerID(layerID: layerID)
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Text Path") {
                core.setTextPath(layerID: layerID,
                                 enabled: newValue != 0,
                                 pathLayerID: newValue,
                                 reversed: core.textPathReversed(layerID: layerID),
                                 perpendicular: core.textPathPerpendicular(layerID: layerID),
                                 forceAlignment: core.textPathForceAlignment(layerID: layerID))
            }
        }
    }

    private var reversedBinding: Binding<Bool> {
        boolBinding(get: { core.textPathReversed(layerID: layerID) }) { value in
            core.setTextPath(layerID: layerID,
                             enabled: core.textPathEnabled(layerID: layerID),
                             pathLayerID: core.textPathLayerID(layerID: layerID),
                             reversed: value,
                             perpendicular: core.textPathPerpendicular(layerID: layerID),
                             forceAlignment: core.textPathForceAlignment(layerID: layerID))
        }
    }

    private var perpendicularBinding: Binding<Bool> {
        boolBinding(get: { core.textPathPerpendicular(layerID: layerID) }) { value in
            core.setTextPath(layerID: layerID,
                             enabled: core.textPathEnabled(layerID: layerID),
                             pathLayerID: core.textPathLayerID(layerID: layerID),
                             reversed: core.textPathReversed(layerID: layerID),
                             perpendicular: value,
                             forceAlignment: core.textPathForceAlignment(layerID: layerID))
        }
    }

    private var forceAlignmentBinding: Binding<Bool> {
        boolBinding(get: { core.textPathForceAlignment(layerID: layerID) }) { value in
            core.setTextPath(layerID: layerID,
                             enabled: core.textPathEnabled(layerID: layerID),
                             pathLayerID: core.textPathLayerID(layerID: layerID),
                             reversed: core.textPathReversed(layerID: layerID),
                             perpendicular: core.textPathPerpendicular(layerID: layerID),
                             forceAlignment: value)
        }
    }

    private func boolBinding(get: @escaping () -> Bool,
                             set: @escaping (Bool) -> Void) -> Binding<Bool>
    {
        Binding {
            get()
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Text Path") {
                set(newValue)
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
                core.endMergeGroup()
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

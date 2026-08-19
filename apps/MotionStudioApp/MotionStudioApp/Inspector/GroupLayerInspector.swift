//
//  GroupLayerInspector.swift
//  MotionStudioApp
//
//  Group-layer controls: corner radius.
//

import SwiftUI

let groupCornerRadiusPath = GroupProperty.cornerRadius.path

struct GroupLayerInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    private var playheadFrame: Int64 {
        clock.frame
    }

    var body: some View {
        let _ = core.panelRevision

        Text("Group")
            .font(.subheadline)
            .foregroundStyle(.secondary)

        NumberPropertyRow(label: "Radius",
                          value: core.evaluateFloat(entityID: layerID,
                                                    path: groupCornerRadiusPath,
                                                    frame: playheadFrame),
                          hasKeyframeAtPlayhead: hasCornerRadiusKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setCornerRadius(value: newValue)
        } onToggleKeyframe: { value in
            toggleCornerRadiusKeyframe(value: value)
        }
    }

    private func hasCornerRadiusKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: groupCornerRadiusPath)
            .contains { $0.frame == playheadFrame }
    }

    private func setCornerRadius(value: Float) {
        guard isEditable else { return }
        let radius = max(value, 0)
        perform("Set Group Corner Radius") {
            if hasCornerRadiusKeyframe() {
                core.addKeyframeFloat(entityID: layerID, path: groupCornerRadiusPath,
                                      frame: playheadFrame, value: radius)
            } else {
                core.setStaticFloat(entityID: layerID, path: groupCornerRadiusPath, value: radius)
            }
        }
    }

    private func toggleCornerRadiusKeyframe(value: Float) {
        guard isEditable else { return }
        if hasCornerRadiusKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: groupCornerRadiusPath,
                                    frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: groupCornerRadiusPath,
                                      frame: playheadFrame, value: max(value, 0))
            }
        }
    }
}

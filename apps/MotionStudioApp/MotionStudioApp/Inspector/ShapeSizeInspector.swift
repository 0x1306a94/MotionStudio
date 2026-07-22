//
//  ShapeSizeInspector.swift
//  MotionStudioApp
//

import SwiftUI

let shapeSizePath = "elements[0].size"

/// Shape geometry editor for rect/ellipse layers: width/height fields with
/// per-property "add keyframe at playhead" buttons.
struct ShapeSizeInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let size = core.evaluateVec2(entityID: layerID, path: shapeSizePath, frame: playheadFrame)

        NumberPropertyRow(label: ShapeSizeField.width.label,
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(),
                          isEditable: isEditable)
        { newValue in
            performSet {
                core.setStaticVec2(entityID: layerID, path: shapeSizePath,
                                   value: CGVector(dx: CGFloat(newValue), dy: size.dy))
            }
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        NumberPropertyRow(label: ShapeSizeField.height.label,
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(),
                          isEditable: isEditable)
        { newValue in
            performSet {
                core.setStaticVec2(entityID: layerID, path: shapeSizePath,
                                   value: CGVector(dx: size.dx, dy: CGFloat(newValue)))
            }
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }
    }

    private func hasKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: shapeSizePath).contains { $0.frame == playheadFrame }
    }

    private func performSet(_ action: () -> Void) {
        guard isEditable else { return }
        perform("Set Size", action)
    }

    private func toggleSizeKeyframe() {
        guard isEditable else { return }
        if hasKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: shapeSizePath, frame: playheadFrame)
            }
        } else {
            let value = core.evaluateVec2(entityID: layerID, path: shapeSizePath, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeVec2(entityID: layerID, path: shapeSizePath,
                                     frame: playheadFrame, value: value)
            }
        }
    }
}

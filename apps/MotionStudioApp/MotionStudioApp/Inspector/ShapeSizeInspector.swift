//
//  ShapeSizeInspector.swift
//  MotionStudioApp
//

import SwiftUI

let shapeSizePath = ShapeProperty.size.path
let shapeCornerRadiusPath = ShapeProperty.cornerRadius.path

/// Shape geometry editor for rect/ellipse layers: width/height fields with
/// per-property "add keyframe at playhead" buttons.
struct ShapeSizeInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let size = core.evaluateVec2(entityID: layerID, path: shapeSizePath, frame: playheadFrame)

        NumberPropertyRow(label: ShapeSizeField.width.label,
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(),
                          isEditable: isEditable,
                          showsStepButtons: true)
        { newValue in
            setSize(value: CGVector(dx: CGFloat(newValue), dy: size.dy))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        NumberPropertyRow(label: ShapeSizeField.height.label,
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(),
                          isEditable: isEditable,
                          showsStepButtons: true)
        { newValue in
            setSize(value: CGVector(dx: size.dx, dy: CGFloat(newValue)))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        if core.hasProperty(entityID: layerID, path: shapeCornerRadiusPath) {
            NumberPropertyRow(label: "Radius",
                              value: core.evaluateFloat(entityID: layerID,
                                                        path: shapeCornerRadiusPath,
                                                        frame: playheadFrame),
                              hasKeyframeAtPlayhead: hasCornerRadiusKeyframe(),
                              isEditable: isEditable)
            { newValue in
                setCornerRadius(value: newValue)
            } onToggleKeyframe: { value in
                toggleCornerRadiusKeyframe(value: value)
            }
        }
    }

    private func hasKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: shapeSizePath).contains { $0.frame == playheadFrame }
    }

    private func hasCornerRadiusKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: shapeCornerRadiusPath).contains { $0.frame == playheadFrame }
    }

    private func setSize(value: CGVector) {
        performSet {
            if hasKeyframe() {
                core.addKeyframeVec2(entityID: layerID, path: shapeSizePath,
                                     frame: playheadFrame, value: value)
            } else {
                core.setStaticVec2(entityID: layerID, path: shapeSizePath, value: value)
            }
        }
    }

    private func performSet(_ action: () -> Void) {
        guard isEditable else { return }
        perform("Set Size", action)
    }

    private func setCornerRadius(value: Float) {
        guard isEditable else { return }
        perform("Set Corner Radius") {
            if hasCornerRadiusKeyframe() {
                core.addKeyframeFloat(entityID: layerID, path: shapeCornerRadiusPath,
                                      frame: playheadFrame, value: value)
            } else {
                core.setStaticFloat(entityID: layerID, path: shapeCornerRadiusPath, value: value)
            }
        }
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

    private func toggleCornerRadiusKeyframe(value: Float) {
        guard isEditable else { return }
        if hasCornerRadiusKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: shapeCornerRadiusPath,
                                    frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: shapeCornerRadiusPath,
                                      frame: playheadFrame, value: value)
            }
        }
    }
}

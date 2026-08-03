//
//  TransformInspector.swift
//  MotionStudioApp
//

import SwiftUI

struct TransformInspector: View {
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
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let followEnabled = core.followPathEnabled(layerID: layerID)
        let followOrients = core.followPathOrient(layerID: layerID)
        let positionEditable = isEditable && !followEnabled
        let rotationEditable = isEditable && !(followEnabled && followOrients)
        let anchor = core.evaluateVec2(entityID: layerID,
                                       path: TransformProperty.anchorPoint.path,
                                       frame: playheadFrame)
        let position = core.evaluateLayoutPosition(compositionID: compositionID,
                                                   layerID: layerID,
                                                   frame: playheadFrame)
        let scale = core.evaluateVec2(entityID: layerID,
                                      path: TransformProperty.scale.path,
                                      frame: playheadFrame)

        if let localBounds = core.layerLocalBounds(compositionID: compositionID,
                                                   layerID: layerID,
                                                   frameTime: Double(playheadFrame)),
            localBounds.width > 0, localBounds.height > 0
        {
            let selectedCorner = AnchorPreset.matchingCorner(anchor: anchor, rect: localBounds)
            AnchorPresetFrame(selected: selectedCorner, isEnabled: isEditable) { corner in
                applyAnchorPreset(corner, localBounds: localBounds)
            }
        }

        NumberPropertyRow(label: TransformField.anchorX.label,
                          value: Float(anchor.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.anchorPoint),
                          isEditable: isEditable,
                          showsStepButtons: true)
        { newValue in
            setVec2Property(.anchorPoint, value: CGVector(dx: CGFloat(newValue), dy: anchor.dy))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.anchorPoint)
        }

        NumberPropertyRow(label: TransformField.anchorY.label,
                          value: Float(anchor.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.anchorPoint),
                          isEditable: isEditable,
                          showsStepButtons: true)
        { newValue in
            setVec2Property(.anchorPoint, value: CGVector(dx: anchor.dx, dy: CGFloat(newValue)))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.anchorPoint)
        }

        NumberPropertyRow(label: TransformField.positionX.label,
                          value: Float(position.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.position),
                          isEditable: positionEditable,
                          showsStepButtons: true)
        { newValue in
            setLayoutPosition(CGVector(dx: CGFloat(newValue), dy: position.dy))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.positionY.label,
                          value: Float(position.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.position),
                          isEditable: positionEditable,
                          showsStepButtons: true)
        { newValue in
            setLayoutPosition(CGVector(dx: position.dx, dy: CGFloat(newValue)))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.scaleX.label,
                          value: Float(scale.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale),
                          isEditable: isEditable)
        { newValue in
            setVec2Property(.scale, value: CGVector(dx: CGFloat(newValue), dy: scale.dy))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.scaleY.label,
                          value: Float(scale.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale),
                          isEditable: isEditable)
        { newValue in
            setVec2Property(.scale, value: CGVector(dx: scale.dx, dy: CGFloat(newValue)))
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.rotation.label,
                          value: core.evaluateFloat(entityID: layerID,
                                                    path: TransformProperty.rotation.path,
                                                    frame: playheadFrame),
                          hasKeyframeAtPlayhead: hasKeyframe(.rotation),
                          isEditable: rotationEditable)
        { newValue in
            setFloatProperty(.rotation, value: newValue)
        } onToggleKeyframe: { value in
            toggleFloatKeyframe(.rotation, value: value)
        }

        NumberPropertyRow(label: TransformField.opacity.label,
                          value: core.evaluateFloat(entityID: layerID,
                                                    path: TransformProperty.opacity.path,
                                                    frame: playheadFrame),
                          hasKeyframeAtPlayhead: hasKeyframe(.opacity),
                          isEditable: isEditable)
        { newValue in
            setFloatProperty(.opacity, value: newValue)
        } onToggleKeyframe: { value in
            toggleFloatKeyframe(.opacity, value: value)
        }
    }

    private func hasKeyframe(_ property: TransformProperty) -> Bool {
        core.keyframes(entityID: layerID, path: property.path).contains { $0.frame == playheadFrame }
    }

    private func applyAnchorPreset(_ corner: AnchorPresetCorner, localBounds: CGRect) {
        guard isEditable else { return }
        let oldAnchor = core.evaluateVec2(entityID: layerID,
                                          path: TransformProperty.anchorPoint.path,
                                          frame: playheadFrame)
        if let match = AnchorPreset.matchingCorner(anchor: oldAnchor, rect: localBounds),
           match == corner
        {
            return
        }
        let newAnchor = AnchorPreset.point(corner: corner, in: localBounds)
        let oldPosition = core.evaluateVec2(entityID: layerID,
                                            path: TransformProperty.position.path,
                                            frame: playheadFrame)
        let scale = core.evaluateVec2(entityID: layerID,
                                      path: TransformProperty.scale.path,
                                      frame: playheadFrame)
        let rotation = core.evaluateFloat(entityID: layerID,
                                          path: TransformProperty.rotation.path,
                                          frame: playheadFrame)
        let newPosition = AnchorPreset.compensatedPosition(oldAnchor: oldAnchor,
                                                           newAnchor: newAnchor,
                                                           position: oldPosition,
                                                           scale: scale,
                                                           rotationDegrees: rotation)
        perform("Set Anchor") {
            // Merge anchor + position into one core undo unit (perform registers a
            // single UI inverse that calls performUndo once).
            core.beginMergeGroup()
            writeVec2(.anchorPoint, value: newAnchor)
            writeVec2(.position, value: newPosition)
            core.endMergeGroup()
        }
    }

    private func writeVec2(_ property: TransformProperty, value: CGVector) {
        if hasKeyframe(property) {
            core.addKeyframeVec2(entityID: layerID, path: property.path,
                                 frame: playheadFrame, value: value)
        } else {
            core.setStaticVec2(entityID: layerID, path: property.path, value: value)
        }
    }

    private func setFloatProperty(_ property: TransformProperty, value: Float) {
        performSet(property) {
            if hasKeyframe(property) {
                core.addKeyframeFloat(entityID: layerID, path: property.path,
                                      frame: playheadFrame, value: value)
            } else {
                core.setStaticFloat(entityID: layerID, path: property.path, value: value)
            }
        }
    }

    private func setVec2Property(_ property: TransformProperty, value: CGVector) {
        performSet(property) {
            writeVec2(property, value: value)
        }
    }

    private func setLayoutPosition(_ value: CGVector) {
        performSet(.position) {
            core.writeLayoutPosition(compositionID: compositionID,
                                     layerID: layerID,
                                     frame: playheadFrame,
                                     value: value)
        }
    }

    private func performSet(_ property: TransformProperty, action: () -> Void) {
        guard isEditable else { return }
        perform("Set \(property.actionLabel)", action)
    }

    private func toggleFloatKeyframe(_ property: TransformProperty, value: Float) {
        guard isEditable else { return }
        if hasKeyframe(property) {
            removeKeyframe(property)
        } else {
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: property.path,
                                      frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleVec2Keyframe(_ property: TransformProperty) {
        guard isEditable else { return }
        if hasKeyframe(property) {
            removeKeyframe(property)
        } else {
            let value = core.evaluateVec2(entityID: layerID, path: property.path, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeVec2(entityID: layerID, path: property.path,
                                     frame: playheadFrame, value: value)
            }
        }
    }

    private func removeKeyframe(_ property: TransformProperty) {
        perform("Delete Keyframe") {
            core.removeKeyframe(entityID: layerID, path: property.path, frame: playheadFrame)
        }
    }
}

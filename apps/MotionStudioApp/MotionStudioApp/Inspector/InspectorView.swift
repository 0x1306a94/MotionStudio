//
//  InspectorView.swift
//  MotionStudioApp
//
//  Property inspector for the selected layer: shape size and transform
//  fields with per-property "add keyframe at playhead" buttons.
//

import Foundation
import SwiftUI

private let shapeSizePath = "elements[0].size"

struct InspectorView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let core = document.core
        if let layerID = editorState.selectedLayerID {
            let _ = core.revision
            let isVisible = core.layerIsVisible(layerID)
            let isLocked = core.layerIsLocked(layerID)
            let isEditable = isVisible && !isLocked
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    Text(core.layerName(layerID))
                        .font(.headline)
                    LayerEditStatus(isVisible: isVisible, isLocked: isLocked)
                    if core.hasProperty(entityID: layerID, path: shapeSizePath) {
                        Text("Shape")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        ShapeSizeInspector(core: core,
                                           layerID: layerID,
                                           playheadFrame: editorState.playheadFrame,
                                           isEditable: isEditable,
                                           perform: perform)
                    }
                    Text("Transform")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    TransformInspector(core: core,
                                       layerID: layerID,
                                       playheadFrame: editorState.playheadFrame,
                                       isEditable: isEditable,
                                       perform: perform)
                }
                .padding(10)
            }
        } else {
            ContentUnavailableView("No Selection",
                                   systemImage: "square.dashed",
                                   description: Text("Select a layer to edit its properties."))
        }
    }
}

private struct LayerEditStatus: View {
    let isVisible: Bool
    let isLocked: Bool

    var body: some View {
        if !isVisible || isLocked {
            HStack(spacing: 6) {
                Image(systemName: statusIcon)
                    .font(.system(size: 11, weight: .semibold))
                Text(statusText)
                    .font(.caption)
                    .fontWeight(.semibold)
            }
            .foregroundStyle(.secondary)
            .padding(.horizontal, 8)
            .padding(.vertical, 5)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 6))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.secondary.opacity(0.22), lineWidth: 1)
            )
        }
    }

    private var statusIcon: String {
        isLocked ? "lock.fill" : "eye.slash"
    }

    private var statusText: String {
        if isLocked && !isVisible {
            return "Locked and Hidden Layer"
        }
        return isLocked ? "Locked Layer" : "Hidden Layer"
    }
}

private struct TransformInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let position = core.evaluateVec2(entityID: layerID,
                                         path: TransformProperty.position.path,
                                         frame: playheadFrame)
        let scale = core.evaluateVec2(entityID: layerID,
                                      path: TransformProperty.scale.path,
                                      frame: playheadFrame)

        NumberPropertyRow(label: TransformField.positionX.label,
                          value: Float(position.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.position),
                          isEditable: isEditable)
        { newValue in
            performSet(.position) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.position.path,
                                   value: CGVector(dx: CGFloat(newValue), dy: position.dy))
            }
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.positionY.label,
                          value: Float(position.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.position),
                          isEditable: isEditable)
        { newValue in
            performSet(.position) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.position.path,
                                   value: CGVector(dx: position.dx, dy: CGFloat(newValue)))
            }
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.scaleX.label,
                          value: Float(scale.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale),
                          isEditable: isEditable)
        { newValue in
            performSet(.scale) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.scale.path,
                                   value: CGVector(dx: CGFloat(newValue), dy: scale.dy))
            }
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.scaleY.label,
                          value: Float(scale.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale),
                          isEditable: isEditable)
        { newValue in
            performSet(.scale) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.scale.path,
                                   value: CGVector(dx: scale.dx, dy: CGFloat(newValue)))
            }
        } onToggleKeyframe: { _ in
            toggleVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.rotation.label,
                          value: core.evaluateFloat(entityID: layerID,
                                                    path: TransformProperty.rotation.path,
                                                    frame: playheadFrame),
                          hasKeyframeAtPlayhead: hasKeyframe(.rotation),
                          isEditable: isEditable)
        { newValue in
            performSet(.rotation) {
                core.setStaticFloat(entityID: layerID, path: TransformProperty.rotation.path, value: newValue)
            }
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
            performSet(.opacity) {
                core.setStaticFloat(entityID: layerID, path: TransformProperty.opacity.path, value: newValue)
            }
        } onToggleKeyframe: { value in
            toggleFloatKeyframe(.opacity, value: value)
        }
    }

    private func hasKeyframe(_ property: TransformProperty) -> Bool {
        core.keyframes(entityID: layerID, path: property.path).contains { $0.frame == playheadFrame }
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

/// Shape geometry editor for rect/ellipse layers: width/height fields with
/// per-property "add keyframe at playhead" buttons.
private struct ShapeSizeInspector: View {
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

/// Numeric field with a toggle keyframe button. The draft mirrors the model
/// value until the user commits with Return.
private struct NumberPropertyRow: View {
    let label: String
    let value: Float
    let hasKeyframeAtPlayhead: Bool
    let isEditable: Bool
    let onCommit: (Float) -> Void
    let onToggleKeyframe: (Float) -> Void

    @State private var draft = ""
    @State private var hasInvalidDraft = false

    var body: some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.callout)
                .frame(width: 78, alignment: .leading)
            // Plain style + explicit border/background: the roundedBorder style
            // renders too faintly to read as a field on Mac Catalyst.
            TextField("", text: $draft)
                .textFieldStyle(.plain)
                .padding(.horizontal, 6)
                .padding(.vertical, 3)
                .background(fieldBackgroundColor, in: RoundedRectangle(cornerRadius: 5))
                .overlay(fieldBorder)
                .onSubmit(commitDraft)
                .disabled(!isEditable)
                .onChange(of: draft) { _, newValue in
                    if hasInvalidDraft, newValue != formattedValue(value) {
                        hasInvalidDraft = false
                    }
                }
            Button {
                toggleKeyframe()
            } label: {
                Image(systemName: hasKeyframeAtPlayhead ? "diamond.fill" : "diamond")
                    .foregroundStyle(hasKeyframeAtPlayhead ? .yellow : .secondary)
            }
            .buttonStyle(.plain)
            .disabled(!isEditable)
            .opacity(isEditable ? 1 : 0.42)
            .help(hasKeyframeAtPlayhead ? "Delete keyframe at playhead" : "Add keyframe at playhead")
        }
        .opacity(isEditable ? 1 : 0.72)
        .onChange(of: value, initial: true) { _, newValue in
            draft = formattedValue(newValue)
            hasInvalidDraft = false
        }
    }

    private var fieldBorder: some View {
        RoundedRectangle(cornerRadius: 5)
            .stroke(fieldBorderColor)
    }

    private var fieldBorderColor: Color {
        if hasInvalidDraft {
            return .red
        }
        return isEditable ? Color.secondary.opacity(0.4) : Color.secondary.opacity(0.18)
    }

    private var fieldBackgroundColor: Color {
        isEditable ? Color(.secondarySystemBackground) : Color.secondary.opacity(0.08)
    }

    private func commitDraft() {
        guard isEditable else {
            rejectDraft()
            return
        }
        guard let committedValue = parsedDraft() else {
            rejectDraft()
            return
        }
        draft = formattedValue(committedValue)
        hasInvalidDraft = false
        if committedValue != value {
            onCommit(committedValue)
        }
    }

    private func toggleKeyframe() {
        guard isEditable else { return }
        guard !hasKeyframeAtPlayhead else {
            onToggleKeyframe(value)
            return
        }
        guard let committedValue = parsedDraft() else {
            rejectDraft()
            return
        }
        draft = formattedValue(committedValue)
        hasInvalidDraft = false
        onToggleKeyframe(committedValue)
    }

    private func parsedDraft() -> Float? {
        let trimmedDraft = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let committedValue = Float(trimmedDraft), committedValue.isFinite else {
            return nil
        }
        return committedValue
    }

    private func rejectDraft() {
        draft = formattedValue(value)
        hasInvalidDraft = true
    }

    private func formattedValue(_ value: Float) -> String {
        var formatted = String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(value))
        while formatted.last == "0" {
            formatted.removeLast()
        }
        if formatted.last == "." {
            formatted.removeLast()
        }
        return formatted
    }
}

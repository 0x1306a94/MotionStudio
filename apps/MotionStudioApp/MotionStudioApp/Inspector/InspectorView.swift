//
//  InspectorView.swift
//  MotionStudioApp
//
//  Property inspector for the selected layer: shape size and transform
//  fields with per-property "add keyframe at playhead" buttons.
//

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
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    Text(core.layerName(layerID))
                        .font(.headline)
                    if core.hasProperty(entityID: layerID, path: shapeSizePath) {
                        Text("Shape")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        ShapeSizeInspector(core: core,
                                           layerID: layerID,
                                           playheadFrame: editorState.playheadFrame,
                                           perform: perform)
                    }
                    Text("Transform")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    TransformInspector(core: core,
                                       layerID: layerID,
                                       playheadFrame: editorState.playheadFrame,
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

private struct TransformInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let position = core.staticVec2(entityID: layerID, path: TransformProperty.position.path)
        let scale = core.staticVec2(entityID: layerID, path: TransformProperty.scale.path)

        NumberPropertyRow(label: TransformField.positionX.label,
                          value: Float(position.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.position))
        { newValue in
            performSet(.position) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.position.path,
                                   value: CGVector(dx: CGFloat(newValue), dy: position.dy))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.positionY.label,
                          value: Float(position.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.position))
        { newValue in
            performSet(.position) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.position.path,
                                   value: CGVector(dx: position.dx, dy: CGFloat(newValue)))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe(.position)
        }

        NumberPropertyRow(label: TransformField.scaleX.label,
                          value: Float(scale.dx),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale))
        { newValue in
            performSet(.scale) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.scale.path,
                                   value: CGVector(dx: CGFloat(newValue), dy: scale.dy))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.scaleY.label,
                          value: Float(scale.dy),
                          hasKeyframeAtPlayhead: hasKeyframe(.scale))
        { newValue in
            performSet(.scale) {
                core.setStaticVec2(entityID: layerID, path: TransformProperty.scale.path,
                                   value: CGVector(dx: scale.dx, dy: CGFloat(newValue)))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe(.scale)
        }

        NumberPropertyRow(label: TransformField.rotation.label,
                          value: core.staticFloat(entityID: layerID, path: TransformProperty.rotation.path),
                          hasKeyframeAtPlayhead: hasKeyframe(.rotation))
        { newValue in
            performSet(.rotation) {
                core.setStaticFloat(entityID: layerID, path: TransformProperty.rotation.path, value: newValue)
            }
        } onAddKeyframe: { value in
            addFloatKeyframe(.rotation, value: value)
        }

        NumberPropertyRow(label: TransformField.opacity.label,
                          value: core.staticFloat(entityID: layerID, path: TransformProperty.opacity.path),
                          hasKeyframeAtPlayhead: hasKeyframe(.opacity))
        { newValue in
            performSet(.opacity) {
                core.setStaticFloat(entityID: layerID, path: TransformProperty.opacity.path, value: newValue)
            }
        } onAddKeyframe: { value in
            addFloatKeyframe(.opacity, value: value)
        }
    }

    private func hasKeyframe(_ property: TransformProperty) -> Bool {
        core.keyframes(entityID: layerID, path: property.path).contains { $0.frame == playheadFrame }
    }

    private func performSet(_ property: TransformProperty, action: () -> Void) {
        perform("Set \(property.actionLabel)", action)
    }

    private func addFloatKeyframe(_ property: TransformProperty, value: Float) {
        perform("Add Keyframe") {
            core.addKeyframeFloat(entityID: layerID, path: property.path,
                                  frame: playheadFrame, value: value)
        }
    }

    private func addVec2Keyframe(_ property: TransformProperty) {
        let value = core.staticVec2(entityID: layerID, path: property.path)
        perform("Add Keyframe") {
            core.addKeyframeVec2(entityID: layerID, path: property.path,
                                 frame: playheadFrame, value: value)
        }
    }
}

/// Shape geometry editor for rect/ellipse layers: width/height fields with
/// per-property "add keyframe at playhead" buttons.
private struct ShapeSizeInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let size = core.staticVec2(entityID: layerID, path: shapeSizePath)

        NumberPropertyRow(label: ShapeSizeField.width.label,
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: hasKeyframe())
        { newValue in
            perform("Set Size") {
                core.setStaticVec2(entityID: layerID, path: shapeSizePath,
                                   value: CGVector(dx: CGFloat(newValue), dy: size.dy))
            }
        } onAddKeyframe: { _ in
            addSizeKeyframe()
        }

        NumberPropertyRow(label: ShapeSizeField.height.label,
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: hasKeyframe())
        { newValue in
            perform("Set Size") {
                core.setStaticVec2(entityID: layerID, path: shapeSizePath,
                                   value: CGVector(dx: size.dx, dy: CGFloat(newValue)))
            }
        } onAddKeyframe: { _ in
            addSizeKeyframe()
        }
    }

    private func hasKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: shapeSizePath).contains { $0.frame == playheadFrame }
    }

    private func addSizeKeyframe() {
        let value = core.staticVec2(entityID: layerID, path: shapeSizePath)
        perform("Add Keyframe") {
            core.addKeyframeVec2(entityID: layerID, path: shapeSizePath,
                                 frame: playheadFrame, value: value)
        }
    }
}

/// Numeric field with a "add keyframe at playhead" button. The draft mirrors
/// the model value until the user commits with Return.
private struct NumberPropertyRow: View {
    let label: String
    let value: Float
    let hasKeyframeAtPlayhead: Bool
    let onCommit: (Float) -> Void
    let onAddKeyframe: (Float) -> Void

    @State private var draft: Float = 0

    var body: some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.callout)
                .frame(width: 78, alignment: .leading)
            // Plain style + explicit border/background: the roundedBorder style
            // renders too faintly to read as a field on Mac Catalyst.
            TextField("", value: $draft, format: .number.precision(.fractionLength(0 ... 2)))
                .textFieldStyle(.plain)
                .padding(.horizontal, 6)
                .padding(.vertical, 3)
                .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 5))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.secondary.opacity(0.4)))
                .onSubmit {
                    if draft != value {
                        onCommit(draft)
                    }
                }
            Button {
                onAddKeyframe(draft)
            } label: {
                Image(systemName: hasKeyframeAtPlayhead ? "diamond.fill" : "diamond")
                    .foregroundStyle(hasKeyframeAtPlayhead ? .yellow : .secondary)
            }
            .buttonStyle(.plain)
            .help("Add keyframe at playhead")
        }
        .onChange(of: value, initial: true) { _, newValue in
            draft = newValue
        }
    }
}

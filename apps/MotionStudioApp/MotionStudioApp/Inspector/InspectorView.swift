//
//  InspectorView.swift
//  MotionStudioApp
//
//  Property inspector for the selected layer: transform fields with
//  per-property "add keyframe at playhead" buttons.
//

import SwiftUI

struct InspectorView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        @Bindable var editorState = editorState
        let core = document.core
        if let layerID = editorState.selectedLayerID {
            let _ = core.revision
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    Text(core.layerName(layerID))
                        .font(.headline)
                    Picker("Timeline lane", selection: $editorState.timelineProperty) {
                        ForEach(TimelineProperty.allCases, id: \.self) { property in
                            Text(property.label).tag(property)
                        }
                    }
                    .pickerStyle(.segmented)
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
        let position = core.staticVec2(entityID: layerID, path: "transform.position")

        NumberPropertyRow(label: "Position X",
                          value: Float(position.dx),
                          hasKeyframeAtPlayhead: hasKeyframe("transform.position"))
        { newValue in
            perform("Set Position") {
                core.setStaticVec2(entityID: layerID, path: "transform.position",
                                   value: CGVector(dx: CGFloat(newValue), dy: position.dy))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe("transform.position")
        }

        NumberPropertyRow(label: "Position Y",
                          value: Float(position.dy),
                          hasKeyframeAtPlayhead: hasKeyframe("transform.position"))
        { newValue in
            perform("Set Position") {
                core.setStaticVec2(entityID: layerID, path: "transform.position",
                                   value: CGVector(dx: position.dx, dy: CGFloat(newValue)))
            }
        } onAddKeyframe: { _ in
            addVec2Keyframe("transform.position")
        }

        NumberPropertyRow(label: "Rotation",
                          value: core.staticFloat(entityID: layerID, path: "transform.rotation"),
                          hasKeyframeAtPlayhead: hasKeyframe("transform.rotation"))
        { newValue in
            perform("Set Rotation") {
                core.setStaticFloat(entityID: layerID, path: "transform.rotation", value: newValue)
            }
        } onAddKeyframe: { value in
            addFloatKeyframe("transform.rotation", value: value)
        }

        NumberPropertyRow(label: "Opacity",
                          value: core.staticFloat(entityID: layerID, path: "transform.opacity"),
                          hasKeyframeAtPlayhead: hasKeyframe("transform.opacity"))
        { newValue in
            perform("Set Opacity") {
                core.setStaticFloat(entityID: layerID, path: "transform.opacity", value: newValue)
            }
        } onAddKeyframe: { value in
            addFloatKeyframe("transform.opacity", value: value)
        }
    }

    private func hasKeyframe(_ path: String) -> Bool {
        core.keyframes(entityID: layerID, path: path).contains { $0.frame == playheadFrame }
    }

    private func addFloatKeyframe(_ path: String, value: Float) {
        perform("Add Keyframe") {
            core.addKeyframeFloat(entityID: layerID, path: path,
                                  frame: playheadFrame, value: value)
        }
    }

    private func addVec2Keyframe(_ path: String) {
        let value = core.staticVec2(entityID: layerID, path: path)
        perform("Add Keyframe") {
            core.addKeyframeVec2(entityID: layerID, path: path,
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
            TextField("", value: $draft, format: .number.precision(.fractionLength(0 ... 2)))
                .textFieldStyle(.roundedBorder)
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

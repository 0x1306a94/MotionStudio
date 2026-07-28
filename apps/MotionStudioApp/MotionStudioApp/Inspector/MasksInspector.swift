//
//  MasksInspector.swift
//  MotionStudioApp
//
//  Path mask list editor: mode, inverted, opacity/feather/expansion with
//  keyframe toggles.
//

import MotionStudioBridging
import SwiftUI

struct MasksInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let playheadFrame: Int64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void
    var onEditMaskPath: ((Int) -> Void)?

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let count = core.maskCount(layerID: layerID)
        HStack {
            Text("Masks")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Button {
                addMask()
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(0 ..< count, id: \.self) { index in
            maskRow(index: index)
                .disabled(!isEditable)
        }
    }

    @ViewBuilder
    private func maskRow(index: Int) -> some View {
        let inverted = core.maskInverted(layerID: layerID, index: index)
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text("Mask \(index + 1)")
                    .font(.callout)
                Picker("", selection: modeBinding(index: index)) {
                    ForEach(MS_MASK.allCases) { mode in
                        Text(mode.label).tag(mode)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .fixedSize()
                .id("mask-mode-\(index)-\(core.revision)")
                Button {
                    setInverted(index: index, inverted: !inverted)
                } label: {
                    Text("Inv")
                        .font(.caption.weight(inverted ? .semibold : .regular))
                        .foregroundStyle(inverted ? Color.accentColor : Color.secondary)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(
                            RoundedRectangle(cornerRadius: 5)
                                .fill(inverted ? Color.accentColor.opacity(0.18)
                                    : Color.secondary.opacity(0.12)),
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 5)
                                .stroke(inverted ? Color.accentColor.opacity(0.7)
                                    : Color.secondary.opacity(0.25), lineWidth: 1),
                        )
                }
                .buttonStyle(.plain)
                .help("Invert mask")
                Spacer(minLength: 0)
                pathKeyframeButton(index: index)
                Button(role: .destructive) {
                    removeMask(index: index)
                } label: {
                    Image(systemName: "minus")
                }
                if let onEditMaskPath {
                    Button {
                        onEditMaskPath(index)
                    } label: {
                        Image(systemName: "pencil.tip")
                    }
                    .help("Edit mask path")
                    .disabled(!isEditable)
                }
            }
            floatPropertyRow(index: index, suffix: "opacity", label: "Opacity")
            floatPropertyRow(index: index, suffix: "feather", label: "Feather")
            floatPropertyRow(index: index, suffix: "expansion", label: "Expansion")
        }
        .padding(.vertical, 2)
        .id("mask-row-\(index)-\(core.revision)")
    }

    private func floatPropertyRow(index: Int, suffix: String, label: String) -> some View {
        let path = maskPath(index: index, suffix: suffix)
        let hasKeyframe = hasKeyframeAtPlayhead(path: path)
        return NumberPropertyRow(label: label,
                                 value: core.evaluateFloat(entityID: layerID, path: path,
                                                           frame: playheadFrame),
                                 hasKeyframeAtPlayhead: hasKeyframe,
                                 isEditable: isEditable)
        { newValue in
            guard isEditable else { return }
            perform("Set Mask \(label)") {
                writeFloat(path: path, value: newValue)
                core.endDrag()
            }
        } onToggleKeyframe: { value in
            guard isEditable else { return }
            if hasKeyframeAtPlayhead(path: path) {
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
        .id("mask-\(path)-\(core.revision)-\(hasKeyframe)")
    }

    private func hasKeyframeAtPlayhead(path: String) -> Bool {
        core.keyframeFrames(entityID: layerID, path: path).contains(playheadFrame)
    }

    @ViewBuilder
    private func pathKeyframeButton(index: Int) -> some View {
        let path = maskPath(index: index, suffix: "path")
        let hasKeyframe = hasKeyframeAtPlayhead(path: path)
        Button {
            guard isEditable else { return }
            if hasKeyframe {
                perform("Delete Keyframe") {
                    core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
                }
            } else {
                perform("Add Keyframe") {
                    core.addKeyframeBezierPathAtPlayhead(entityID: layerID, path: path,
                                                         frame: playheadFrame)
                }
            }
        } label: {
            Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                .id(hasKeyframe)
        }
        .buttonStyle(.plain)
        .disabled(!isEditable)
        .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
    }

    private func writeFloat(path: String, value: Float) {
        if hasKeyframeAtPlayhead(path: path) {
            core.addKeyframeFloat(entityID: layerID, path: path,
                                  frame: playheadFrame, value: value)
        } else {
            core.setStaticFloat(entityID: layerID, path: path, value: value)
        }
    }

    private func setInverted(index: Int, inverted: Bool) {
        guard isEditable else { return }
        perform("Set Mask Inverted") {
            core.setMaskInverted(layerID: layerID, index: index, inverted: inverted)
        }
    }

    private func modeBinding(index: Int) -> Binding<MS_MASK> {
        Binding {
            let mode = core.maskMode(layerID: layerID, index: index)
            return mode == .INVALID ? .ADD : mode
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Mask Mode") {
                core.setMaskMode(layerID: layerID, index: index, mode: newValue)
            }
        }
    }

    private func maskPath(index: Int, suffix: String) -> String {
        "masks[\(index)].\(suffix)"
    }

    private func addMask() {
        guard isEditable else { return }
        perform("Add Mask") {
            core.addMask(layerID: layerID, frame: playheadFrame)
        }
    }

    private func removeMask(index: Int) {
        guard isEditable else { return }
        perform("Remove Mask") {
            core.removeMask(layerID: layerID, index: index)
        }
    }
}

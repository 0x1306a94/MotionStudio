//
//  FillsInspector.swift
//  MotionStudioApp
//
//  Fill style list editor for shape layers.
//

import SwiftUI

/// One row per fill (color, blend mode, delete) plus an add button in the
/// section header. Fills are addressed by their index in the layer's style
/// list, matching the "styles[N]" property paths.
struct FillsInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let fills = fillIndices()
        HStack {
            Text("Fills")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Button {
                addFill()
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(Array(fills.enumerated()), id: \.element) { position, styleIndex in
            HStack(spacing: 8) {
                ColorPicker("Fill \(position + 1)",
                            selection: colorBinding(styleIndex: styleIndex),
                            supportsOpacity: true)
                    .font(.callout)
                Picker("", selection: blendBinding(styleIndex: styleIndex)) {
                    ForEach(FillBlendMode.allCases) { mode in
                        Text(mode.label).tag(mode)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .fixedSize()
                Button(role: .destructive) {
                    removeFill(styleIndex: styleIndex)
                } label: {
                    Image(systemName: "minus")
                }
            }
            .disabled(!isEditable)
        }
    }

    private func fillIndices() -> [Int] {
        (0 ..< core.styleCount(layerID: layerID)).filter { index in
            core.styleType(layerID: layerID, index: index) == MS_STYLE_FILL
        }
    }

    private func fillColorPath(styleIndex: Int) -> String {
        "styles[\(styleIndex)].color"
    }

    private func colorBinding(styleIndex: Int) -> Binding<Color> {
        Binding {
            core.staticColor(entityID: layerID, path: fillColorPath(styleIndex: styleIndex)).swiftUIColor
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Fill Color") {
                core.setStaticColor(entityID: layerID,
                                    path: fillColorPath(styleIndex: styleIndex),
                                    value: MotionColor(newValue).clampedChannels())
                core.endDrag()
            }
        }
    }

    private func blendBinding(styleIndex: Int) -> Binding<FillBlendMode> {
        Binding {
            FillBlendMode(rawValue: core.styleBlendMode(layerID: layerID, index: styleIndex)) ?? .normal
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Fill Blend Mode") {
                core.setStyleBlendMode(layerID: layerID, index: styleIndex,
                                       blendMode: newValue.rawValue)
            }
        }
    }

    private func addFill() {
        guard isEditable else { return }
        perform("Add Fill") {
            core.addFillStyle(layerID: layerID)
        }
    }

    private func removeFill(styleIndex: Int) {
        guard isEditable else { return }
        perform("Remove Fill") {
            core.removeStyle(layerID: layerID, index: styleIndex)
        }
    }
}

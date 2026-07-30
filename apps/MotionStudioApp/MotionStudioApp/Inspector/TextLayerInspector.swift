//
//  TextLayerInspector.swift
//  MotionStudioApp
//
//  Text-layer controls: copy, font, box size, alignment, and paint styles.
//

import MotionStudioBridging
import SwiftUI

let textSizePath = TextProperty.size.path
let textFontSizePath = TextProperty.fontSize.path
let textStringPath = TextProperty.text.path

struct TextLayerInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    @State private var draftText = ""
    @FocusState private var textFocused: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    var body: some View {
        let _ = core.revision
        let fonts = core.fontAssetIDs()
        let boundAssetID = core.textFontAssetID(layerID: layerID)
        let selectedAssetName = boundAssetID == 0 ? "None" : core.assetName(boundAssetID)
        let size = core.evaluateVec2(entityID: layerID, path: textSizePath, frame: playheadFrame)
        let fontSize = core.evaluateFloat(entityID: layerID, path: textFontSizePath, frame: playheadFrame)
        let autoHeight = core.textAutoHeight(layerID: layerID)
        let align = core.textAlign(layerID: layerID)
        let fontFamily = core.textFontFamily(layerID: layerID)
        let text = core.staticString(entityID: layerID, path: textStringPath)
        let blendMode = core.layerBlendMode(layerID: layerID)

        Text("Text")
            .font(.subheadline)
            .foregroundStyle(.secondary)

        VStack(alignment: .leading, spacing: 6) {
            Text("Content")
                .font(.caption)
                .foregroundStyle(.secondary)
            TextField("Text", text: $draftText, axis: .vertical)
                .lineLimit(3 ... 8)
                .disabled(!isEditable)
                .focused($textFocused)
                .onAppear {
                    draftText = text
                }
                .onChange(of: text) { _, newValue in
                    if !textFocused {
                        draftText = newValue
                    }
                }
                .onChange(of: textFocused) { _, focused in
                    if focused {
                        core.beginDrag()
                    } else {
                        commitText()
                        core.endDrag()
                    }
                }
        }
        .font(.subheadline)

        NumberPropertyRow(label: "Font Size",
                          value: fontSize,
                          hasKeyframeAtPlayhead: hasFontSizeKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setFontSize(value: max(1, newValue))
        } onToggleKeyframe: { _ in
            toggleFontSizeKeyframe()
        }

        NumberPropertyRow(label: "Width",
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: hasSizeKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setSize(value: CGVector(dx: CGFloat(max(1, newValue)), dy: size.dy))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        NumberPropertyRow(label: "Height",
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: hasSizeKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setSize(value: CGVector(dx: size.dx, dy: CGFloat(max(1, newValue))))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        Toggle("Auto Height", isOn: Binding(
            get: { autoHeight },
            set: { newValue in
                guard isEditable else { return }
                perform("Set Text Auto Height") {
                    core.setTextAutoHeight(layerID: layerID, autoHeight: newValue)
                }
            },
        ))
        .disabled(!isEditable)
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Align")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Picker("Align", selection: Binding(
                get: { align },
                set: { newValue in
                    guard isEditable else { return }
                    perform("Set Text Align") {
                        core.setTextAlign(layerID: layerID, align: newValue)
                    }
                },
            )) {
                ForEach(MS_TEXT_ALIGN.allCases) { option in
                    Text(option.label).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable)
        }
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Font Family")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            TextField("Font Family", text: Binding(
                get: { fontFamily },
                set: { newValue in
                    guard isEditable else { return }
                    perform("Set Text Font Family") {
                        core.setTextFontFamily(layerID: layerID, family: newValue)
                    }
                },
            ))
            .multilineTextAlignment(.trailing)
            .disabled(!isEditable)
            .frame(maxWidth: 160)
        }
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Font Asset")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Menu {
                Button {
                    setFontAsset(0)
                } label: {
                    assetOptionLabel("None")
                }
                ForEach(fonts, id: \.self) { assetID in
                    Button {
                        setFontAsset(assetID)
                    } label: {
                        assetOptionLabel(core.assetName(assetID))
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Text(selectedAssetName)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .frame(maxWidth: .infinity, alignment: .trailing)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 5)
                .frame(width: Self.assetPickerMaxWidth, alignment: .leading)
            }
            .disabled(!isEditable)
            .frame(width: Self.assetPickerMaxWidth, alignment: .trailing)
            .clipped()
        }
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Blend Mode")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Picker("Blend Mode", selection: Binding(
                get: { blendMode == .INVALID ? .NORMAL : blendMode },
                set: { newValue in
                    guard isEditable else { return }
                    perform("Set Layer Blend Mode") {
                        core.setLayerBlendMode(layerID: layerID, blendMode: newValue)
                    }
                },
            )) {
                ForEach(MS_BLEND.allCases) { option in
                    Text(option.label).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable)
        }
        .font(.subheadline)
    }

    private static let assetPickerMaxWidth: CGFloat = 168

    private func assetOptionLabel(_ title: String) -> some View {
        Text(title)
            .lineLimit(1)
            .truncationMode(.middle)
            .frame(maxWidth: Self.assetPickerMaxWidth, alignment: .trailing)
    }

    private func commitText() {
        guard isEditable else { return }
        let current = core.staticString(entityID: layerID, path: textStringPath)
        guard draftText != current else { return }
        perform("Set Text") {
            core.setStaticString(entityID: layerID, path: textStringPath, value: draftText)
        }
    }

    private func setFontAsset(_ assetID: UInt64) {
        guard isEditable else { return }
        perform("Set Text Font Asset") {
            _ = core.setTextFontAsset(layerID: layerID, assetID: assetID)
        }
    }

    private func hasSizeKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: textSizePath).contains { $0.frame == playheadFrame }
    }

    private func hasFontSizeKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: textFontSizePath).contains { $0.frame == playheadFrame }
    }

    private func setSize(value: CGVector) {
        guard isEditable else { return }
        perform("Set Text Size") {
            if hasSizeKeyframe() {
                let oldSize = core.evaluateVec2(entityID: layerID, path: textSizePath, frame: playheadFrame)
                let oldAnchor = core.evaluateVec2(entityID: layerID,
                                                  path: TransformProperty.anchorPoint.path,
                                                  frame: playheadFrame)
                let ratioX = oldSize.dx > 1e-6 ? value.dx / oldSize.dx : 1
                let ratioY = oldSize.dy > 1e-6 ? value.dy / oldSize.dy : 1
                let newAnchor = CGVector(dx: oldAnchor.dx * ratioX, dy: oldAnchor.dy * ratioY)
                core.beginDrag()
                core.addKeyframeVec2(entityID: layerID, path: textSizePath,
                                     frame: playheadFrame, value: value)
                if core.isAnimated(entityID: layerID, path: TransformProperty.anchorPoint.path) {
                    core.addKeyframeVec2(entityID: layerID, path: TransformProperty.anchorPoint.path,
                                         frame: playheadFrame, value: newAnchor)
                } else {
                    core.setStaticVec2(entityID: layerID, path: TransformProperty.anchorPoint.path,
                                       value: newAnchor)
                }
                core.endDrag()
            } else {
                core.setTextBoxSize(layerID: layerID, size: value, frame: playheadFrame)
            }
        }
    }

    private func setFontSize(value: Float) {
        guard isEditable else { return }
        perform("Set Font Size") {
            if hasFontSizeKeyframe() {
                core.addKeyframeFloat(entityID: layerID, path: textFontSizePath,
                                      frame: playheadFrame, value: value)
            } else {
                core.setStaticFloat(entityID: layerID, path: textFontSizePath, value: value)
            }
        }
    }

    private func toggleSizeKeyframe() {
        guard isEditable else { return }
        if hasSizeKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: textSizePath, frame: playheadFrame)
            }
        } else {
            let value = core.evaluateVec2(entityID: layerID, path: textSizePath, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeVec2(entityID: layerID, path: textSizePath,
                                     frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleFontSizeKeyframe() {
        guard isEditable else { return }
        if hasFontSizeKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: textFontSizePath, frame: playheadFrame)
            }
        } else {
            let value = core.evaluateFloat(entityID: layerID, path: textFontSizePath, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: textFontSizePath,
                                      frame: playheadFrame, value: value)
            }
        }
    }
}

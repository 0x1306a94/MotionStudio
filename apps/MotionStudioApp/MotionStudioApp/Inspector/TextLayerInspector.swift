//
//  TextLayerInspector.swift
//  MotionStudioApp
//
//  Text-layer controls: copy, font, box size, alignment, and paint styles.
//

import MotionStudioBridging
import SwiftUI
import UIKit

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

    private var systemFontFaces: [String] {
        UIFont.familyNames
            .flatMap { UIFont.fontNames(forFamilyName: $0) }
            .sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }

    var body: some View {
        let _ = core.revision
        let size = core.evaluateVec2(entityID: layerID, path: textSizePath, frame: playheadFrame)
        let fontSize = core.evaluateFloat(entityID: layerID, path: textFontSizePath, frame: playheadFrame)
        let autoHeight = core.textAutoHeight(layerID: layerID)
        let align = core.textAlign(layerID: layerID)
        let fontFamily = core.textFontFamily(layerID: layerID)
        let fontStyle = core.textFontStyle(layerID: layerID)
        let text = core.staticString(entityID: layerID, path: textStringPath)
        let selectedFace = displayFace(family: fontFamily, style: fontStyle)
        let faces = resolvedFontFaces(current: selectedFace)

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
            Text("Font")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Menu {
                ForEach(faces, id: \.self) { face in
                    Button {
                        setFont(face)
                    } label: {
                        fontOptionLabel(face)
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Text(selectedFace.isEmpty ? "—" : selectedFace)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .frame(maxWidth: .infinity, alignment: .trailing)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 5)
                .frame(width: Self.fontPickerMaxWidth, alignment: .leading)
            }
            .disabled(!isEditable)
            .frame(width: Self.fontPickerMaxWidth, alignment: .trailing)
            .clipped()
        }
        .font(.subheadline)
    }

    private static let fontPickerMaxWidth: CGFloat = 168

    private func fontOptionLabel(_ title: String) -> some View {
        Text(title)
            .lineLimit(1)
            .truncationMode(.middle)
            .frame(maxWidth: Self.fontPickerMaxWidth, alignment: .trailing)
    }

    private func resolvedFontFaces(current: String) -> [String] {
        var faces = systemFontFaces
        if !current.isEmpty, !faces.contains(current) {
            faces.insert(current, at: 0)
        }
        return faces
    }

    private func displayFace(family: String, style: String) -> String {
        for face in systemFontFaces {
            let resolved = Self.resolveFace(face)
            if resolved.family == family, Self.stylesEqual(resolved.style, style) {
                return face
            }
        }
        if style.isEmpty {
            return family
        }
        return "\(family)-\(style)"
    }

    private func setFont(_ face: String) {
        guard isEditable else { return }
        let resolved = Self.resolveFace(face)
        perform("Set Text Font") {
            core.setTextFont(layerID: layerID, family: resolved.family, style: resolved.style)
        }
    }

    private static func resolveFace(_ face: String) -> (family: String, style: String) {
        guard let font = UIFont(name: face, size: 12) else {
            return (face, "")
        }
        let style = (font.fontDescriptor.object(forKey: .face) as? String) ?? ""
        return (font.familyName, style)
    }

    private static func stylesEqual(_ left: String, _ right: String) -> Bool {
        normalizeStyle(left) == normalizeStyle(right)
    }

    private static func normalizeStyle(_ style: String) -> String {
        let trimmed = style.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            return ""
        }
        if trimmed.caseInsensitiveCompare("Regular") == .orderedSame ||
            trimmed.caseInsensitiveCompare("Normal") == .orderedSame
        {
            return ""
        }
        return trimmed
    }

    private func commitText() {
        guard isEditable else { return }
        let current = core.staticString(entityID: layerID, path: textStringPath)
        guard draftText != current else { return }
        perform("Set Text") {
            core.setStaticString(entityID: layerID, path: textStringPath, value: draftText)
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

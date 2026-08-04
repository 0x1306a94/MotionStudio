//
//  TextLayerInspector.swift
//  MotionStudioApp
//
//  Text-layer controls: copy, font, box size, alignment, and paint styles.
//

import MotionStudioBridging
import SwiftUI
import UIKit

let textStringPath = TextProperty.text.path

struct TextLayerInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
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
        let size = core.textSize(layerID: layerID)
        let fontSize = core.textFontSize(layerID: layerID)
        let boxTextMode = core.textBoxTextMode(layerID: layerID)
        let textPathActive = core.textPathEnabled(layerID: layerID) &&
            core.textPathLayerID(layerID: layerID) != 0
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
                        core.beginMergeGroup()
                    } else {
                        commitText()
                        core.endMergeGroup()
                    }
                }
        }
        .font(.subheadline)

        NumberPropertyRow(label: "Font Size",
                          value: fontSize,
                          hasKeyframeAtPlayhead: false,
                          isEditable: isEditable,
                          showsKeyframeButton: false)
        { newValue in
            setFontSize(value: max(1, newValue))
        } onToggleKeyframe: { _ in }

        NumberPropertyRow(label: "Width",
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: false,
                          isEditable: isEditable && boxTextMode && !textPathActive,
                          showsKeyframeButton: false)
        { newValue in
            setSize(value: CGVector(dx: CGFloat(max(1, newValue)), dy: size.dy))
        } onToggleKeyframe: { _ in }

        NumberPropertyRow(label: "Height",
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: false,
                          isEditable: isEditable && boxTextMode && !textPathActive,
                          showsKeyframeButton: false)
        { newValue in
            setSize(value: CGVector(dx: size.dx, dy: CGFloat(max(1, newValue))))
        } onToggleKeyframe: { _ in }

        Toggle("框文本模式", isOn: Binding(
            get: { boxTextMode },
            set: { newValue in
                guard isEditable else { return }
                perform("Set Text Box Text Mode") {
                    core.setTextBoxTextMode(layerID: layerID, boxTextMode: newValue, frame: playheadFrame)
                }
            },
        ))
        .disabled(!isEditable || textPathActive)
        .font(.subheadline)

        TextPathInspector(core: core,
                          compositionID: compositionID,
                          layerID: layerID,
                          isEditable: isEditable,
                          perform: perform)

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

    private func setSize(value: CGVector) {
        guard isEditable, core.textBoxTextMode(layerID: layerID) else { return }
        perform("Set Text Size") {
            core.setTextBoxSize(layerID: layerID, size: value, frame: playheadFrame)
        }
    }

    private func setFontSize(value: Float) {
        guard isEditable else { return }
        perform("Set Font Size") {
            core.setTextFontSize(layerID: layerID, fontSize: value)
        }
    }
}

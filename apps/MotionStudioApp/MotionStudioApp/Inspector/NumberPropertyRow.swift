//
//  NumberPropertyRow.swift
//  MotionStudioApp
//

import Foundation
import SwiftUI

/// Numeric field with a toggle keyframe button. The draft mirrors the model
/// value until the user commits with Return or the field loses focus.
struct NumberPropertyRow: View {
    let label: String
    let value: Float
    let hasKeyframeAtPlayhead: Bool
    let isEditable: Bool
    var showsKeyframeButton = true
    let onCommit: (Float) -> Void
    let onToggleKeyframe: (Float) -> Void

    @State private var draft = ""
    @State private var hasInvalidDraft = false
    @FocusState private var isFieldFocused: Bool

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
                .focused($isFieldFocused)
                .onChange(of: draft) { _, newValue in
                    let filtered = filteredNumericDraft(newValue)
                    if filtered != newValue {
                        draft = filtered
                        return
                    }
                    if hasInvalidDraft, newValue != formattedValue(value) {
                        hasInvalidDraft = false
                    }
                }
                .onChange(of: isFieldFocused) { _, focused in
                    if !focused {
                        commitDraftOnFocusLoss()
                    }
                }
            if showsKeyframeButton {
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
            } else {
                Color.clear
                    .frame(width: 16, height: 16)
            }
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

    private func commitDraftOnFocusLoss() {
        commitDraft()
        hasInvalidDraft = false
    }

    private func filteredNumericDraft(_ input: String) -> String {
        var filtered = ""
        var hasDecimalPoint = false
        for character in input {
            if character.isASCII, character.isNumber {
                filtered.append(character)
            } else if character == ".", !hasDecimalPoint {
                hasDecimalPoint = true
                filtered.append(character)
            } else if character == "-", filtered.isEmpty {
                filtered.append(character)
            }
        }
        return filtered
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

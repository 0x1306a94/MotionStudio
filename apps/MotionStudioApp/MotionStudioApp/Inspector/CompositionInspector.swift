//
//  CompositionInspector.swift
//  MotionStudioApp
//

import SwiftUI
#if canImport(UIKit)
    import UIKit
#endif

struct CompositionInspector: View {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let _ = core.panelRevision
        let color = core.backgroundColor(compositionID: compositionID)
        let size = core.size(compositionID: compositionID)

        VStack(alignment: .leading, spacing: 10) {
            Text(core.compositionName(compositionID))
                .font(.headline)
            NumberPropertyRow(label: "Width",
                              value: Float(size.width),
                              hasKeyframeAtPlayhead: false,
                              isEditable: true,
                              showsKeyframeButton: false)
            { newValue in
                setSize(width: newValue, height: Float(size.height))
            } onToggleKeyframe: { _ in }

            NumberPropertyRow(label: "Height",
                              value: Float(size.height),
                              hasKeyframeAtPlayhead: false,
                              isEditable: true,
                              showsKeyframeButton: false)
            { newValue in
                setSize(width: Float(size.width), height: newValue)
            } onToggleKeyframe: { _ in }

            NumberPropertyRow(label: "Duration",
                              value: Float(core.duration(compositionID: compositionID)),
                              hasKeyframeAtPlayhead: false,
                              isEditable: true,
                              showsKeyframeButton: false)
            { newValue in
                setDuration(newValue)
            } onToggleKeyframe: { _ in }

            NumberPropertyRow(label: "FPS",
                              value: Float(core.frameRate(compositionID: compositionID)),
                              hasKeyframeAtPlayhead: false,
                              isEditable: true,
                              showsKeyframeButton: false)
            { newValue in
                setFrameRate(newValue)
            } onToggleKeyframe: { _ in }

            ColorPicker("Background",
                        selection: backgroundColorBinding(color: color),
                        supportsOpacity: true)
                .font(.callout)

            NumberPropertyRow(label: "Radius",
                              value: core.cornerRadius(compositionID: compositionID),
                              hasKeyframeAtPlayhead: false,
                              isEditable: true,
                              showsKeyframeButton: false)
            { newValue in
                setCornerRadius(newValue)
            } onToggleKeyframe: { _ in }
        }
    }

    private func setBackgroundColor(_ color: MotionColor) {
        perform("Set Background") {
            core.setCompositionBackgroundColor(compositionID: compositionID,
                                               value: color.clampedChannels())
            core.endMergeGroup()
        }
    }

    private func setCornerRadius(_ value: Float) {
        perform("Set Composition Corner Radius") {
            core.setCompositionCornerRadius(compositionID: compositionID, value: max(value, 0))
        }
    }

    private func setSize(width: Float, height: Float) {
        perform("Set Composition Size") {
            core.setCompositionSize(compositionID: compositionID,
                                    size: CGSize(width: CGFloat(max(width, 1)),
                                                 height: CGFloat(max(height, 1))))
        }
    }

    private func setDuration(_ value: Float) {
        perform("Set Duration") {
            core.setCompositionDuration(compositionID: compositionID,
                                        duration: Int64(max(value.rounded(), 1)))
        }
    }

    private func setFrameRate(_ value: Float) {
        perform("Set Frame Rate") {
            core.setCompositionFrameRate(compositionID: compositionID,
                                         framesPerSecond: max(value, 0.001))
        }
    }

    private func backgroundColorBinding(color: MotionColor) -> Binding<Color> {
        Binding {
            color.swiftUIColor
        } set: { newValue in
            setBackgroundColor(MotionColor(newValue))
        }
    }
}

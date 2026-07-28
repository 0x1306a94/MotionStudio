//
//  KeyframeEasingPopover.swift
//  MotionStudioApp
//
//  Popover to edit a keyframe's outgoing time easing (presets + custom curve).
//

import MotionStudioBridging
import SwiftUI

struct KeyframeEasingPopover: View {
    let easing: EasingInfo
    /// False for the last keyframe (no following segment).
    let easingAffectsPlayback: Bool
    let onSetEasing: (EasingInfo) -> Void
    let onDelete: (() -> Void)?
    let onCommit: () -> Void
    let onDragBegan: () -> Void
    let onDragEnded: () -> Void

    @State private var showCustomPad: Bool
    @State private var isDraggingPad = false

    init(easing: EasingInfo,
         easingAffectsPlayback: Bool,
         onSetEasing: @escaping (EasingInfo) -> Void,
         onDelete: (() -> Void)?,
         onCommit: @escaping () -> Void,
         onDragBegan: @escaping () -> Void,
         onDragEnded: @escaping () -> Void)
    {
        self.easing = easing
        self.easingAffectsPlayback = easingAffectsPlayback
        self.onSetEasing = onSetEasing
        self.onDelete = onDelete
        self.onCommit = onCommit
        self.onDragBegan = onDragBegan
        self.onDragEnded = onDragEnded
        _showCustomPad = State(initialValue: easing.kind == .CUBIC_BEZIER)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Easing")
                .font(.headline)

            if !easingAffectsPlayback {
                Text("No following keyframe — easing is unused until you add one.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Group {
                presetButton("Linear", .linear)
                presetButton("Ease", .ease)
                presetButton("Ease In", .easeIn)
                presetButton("Ease Out", .easeOut)
                presetButton("Ease In Out", .easeInOut)
                presetButton("Hold", .hold)

                Button {
                    showCustomPad = true
                    if easing.kind != .CUBIC_BEZIER {
                        onSetEasing(EasingInfo.custom(inX: 0.25, inY: 0.1, outX: 0.25, outY: 1))
                        onCommit()
                    }
                } label: {
                    HStack {
                        Text("Custom")
                        Spacer()
                        if showCustomPad || easing.kind == .CUBIC_BEZIER {
                            Image(systemName: "checkmark")
                        }
                    }
                }
                .buttonStyle(.plain)
            }
            .disabled(!easingAffectsPlayback)
            .opacity(easingAffectsPlayback ? 1 : 0.45)

            if easingAffectsPlayback, showCustomPad || easing.kind == .CUBIC_BEZIER {
                let points = Self.padPoints(from: easing)
                CubicBezierPad(p0: points.p0,
                               p3: points.p3,
                               c1: points.c1,
                               c2: points.c2,
                               isEditable: true)
                { c1, c2 in
                    if !isDraggingPad {
                        isDraggingPad = true
                        onDragBegan()
                    }
                    onSetEasing(Self.easing(fromPadC1: c1, c2: c2))
                } onDragEnded: {
                    guard isDraggingPad else { return }
                    isDraggingPad = false
                    onDragEnded()
                }
                .frame(width: 200)
            }

            if let onDelete {
                Divider()
                Button("Delete Keyframe", role: .destructive, action: onDelete)
            }
        }
        .padding(12)
        .frame(minWidth: 220)
    }

    private func presetButton(_ title: String, _ value: EasingInfo) -> some View {
        Button {
            showCustomPad = false
            onSetEasing(value)
            onCommit()
        } label: {
            HStack {
                Text(title)
                Spacer()
                if !showCustomPad, easing.kind == value.kind, easing.kind != .CUBIC_BEZIER {
                    Image(systemName: "checkmark")
                }
            }
        }
        .buttonStyle(.plain)
    }

    /// Content space: x = time, y increases downward; CSS y is flipped.
    private static func padPoints(from easing: EasingInfo) -> (p0: CGPoint, p3: CGPoint,
                                                               c1: CGPoint, c2: CGPoint)
    {
        let inX = CGFloat(max(0, min(1, easing.inX)))
        let outX = CGFloat(max(0, min(1, easing.outX)))
        return (CGPoint(x: 0, y: 1),
                CGPoint(x: 1, y: 0),
                CGPoint(x: inX, y: 1 - CGFloat(easing.inY)),
                CGPoint(x: outX, y: 1 - CGFloat(easing.outY)))
    }

    private static func easing(fromPadC1 c1: CGPoint, c2: CGPoint) -> EasingInfo {
        let inX = Float(max(0, min(1, c1.x)))
        let outX = Float(max(0, min(1, c2.x)))
        let inY = Float(1 - c1.y)
        let outY = Float(1 - c2.y)
        return EasingInfo.custom(inX: inX, inY: inY, outX: outX, outY: outY)
    }
}

extension EasingInfo {
    static func custom(inX: Float, inY: Float, outX: Float, outY: Float) -> EasingInfo {
        EasingInfo(kind: .CUBIC_BEZIER, inX: inX, inY: inY, outX: outX, outY: outY)
    }
}

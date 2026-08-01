//
//  AnchorPresetFrame.swift
//  MotionStudioApp
//

import SwiftUI

struct AnchorPresetFrame: View {
    let selected: AnchorPresetCorner?
    let isEnabled: Bool
    let onSelect: (AnchorPresetCorner) -> Void

    private let boxSize: CGFloat = 44
    private let dotRadius: CGFloat = 3

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 2)
                .strokeBorder(Color.secondary.opacity(0.55), lineWidth: 1)
                .frame(width: boxSize, height: boxSize)
            ForEach(AnchorPresetCorner.allCases, id: \.self) { corner in
                Button {
                    onSelect(corner)
                } label: {
                    Circle()
                        .strokeBorder(selected == corner ? Color.accentColor : Color.secondary, lineWidth: 1)
                        .background(
                            Circle().fill(selected == corner ? Color.accentColor : Color.clear),
                        )
                        .frame(width: dotRadius * 2, height: dotRadius * 2)
                        .frame(width: 22, height: 22)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .position(dotPosition(corner))
            }
        }
        .frame(width: boxSize + 12, height: boxSize + 12)
        .disabled(!isEnabled)
        .opacity(isEnabled ? 1 : 0.45)
        .accessibilityLabel("Anchor preset")
    }

    private func dotPosition(_ corner: AnchorPresetCorner) -> CGPoint {
        let inset: CGFloat = 6
        let origin = CGPoint(x: inset, y: inset)
        let size = boxSize
        let x: CGFloat
        let y: CGFloat
        switch corner {
        case .topLeft, .middleLeft, .bottomLeft: x = origin.x
        case .topCenter, .center, .bottomCenter: x = origin.x + size / 2
        case .topRight, .middleRight, .bottomRight: x = origin.x + size
        }
        switch corner {
        case .topLeft, .topCenter, .topRight: y = origin.y
        case .middleLeft, .center, .middleRight: y = origin.y + size / 2
        case .bottomLeft, .bottomCenter, .bottomRight: y = origin.y + size
        }
        return CGPoint(x: x, y: y)
    }
}

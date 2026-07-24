//
//  KeyframeDiamond.swift
//  MotionStudioApp
//
//  Keyframe marker drawing and interactions.
//

import SwiftUI

struct KeyframeDiamond: View {
    let keyframe: KeyframeInfo
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isSelected: Bool
    let onMove: (Int64, Int64) -> Void
    let onMoveEnded: () -> Void
    let onDelete: () -> Void
    let onSetEasing: (EasingInfo) -> Void

    @State private var dragStartFrame: Int64?
    @State private var isHovering = false

    var body: some View {
        Image(systemName: "diamond.fill")
            .font(.system(size: isHovering ? 12 : 11))
            .foregroundStyle(isSelected ? Color.accentColor : Color.premnitiplyColor(gray: 1, alpha: 0.68))
            .shadow(color: isHovering ? .black.opacity(0.18) : .clear,
                    radius: 2, y: 1)
            .frame(width: 20, height: propertyRowHeight)
            .contentShape(Rectangle())
            .onHover { isHovering = $0 }
            .position(x: trackLeadingInset + timelineX(for: keyframe.frame, pointsPerFrame: pointsPerFrame) - scrollX,
                      y: propertyRowHeight / 2)
            .gesture(
                DragGesture()
                    .onChanged { value in
                        if dragStartFrame == nil {
                            dragStartFrame = keyframe.frame
                        }
                        guard let start = dragStartFrame else { return }
                        let target = Int64(
                            (CGFloat(start) + value.translation.width / pointsPerFrame).rounded(),
                        )
                        let clamped = min(max(target, 0), duration)
                        if clamped != keyframe.frame {
                            onMove(keyframe.frame, clamped)
                        }
                    }
                    .onEnded { _ in
                        dragStartFrame = nil
                        onMoveEnded()
                    },
            )
            .contextMenu {
                Button("Delete Keyframe", role: .destructive, action: onDelete)
                Divider()
                Button("Linear") { onSetEasing(.linear) }
                Button("Ease") { onSetEasing(.ease) }
                Button("Ease In") { onSetEasing(.easeIn) }
                Button("Ease Out") { onSetEasing(.easeOut) }
                Button("Ease In Out") { onSetEasing(.easeInOut) }
                Button("Hold") { onSetEasing(.hold) }
            }
    }
}

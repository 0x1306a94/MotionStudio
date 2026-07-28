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
    /// False when this is the last keyframe on the track (no outgoing segment).
    let hasOutgoingSegment: Bool
    let onMove: (Int64, Int64) -> Void
    let onMoveEnded: () -> Void
    let onDelete: () -> Void
    let onSetEasing: (EasingInfo) -> Void
    let onEasingCommit: () -> Void
    let onEasingDragBegan: () -> Void
    let onEasingDragEnded: () -> Void

    @State private var dragStartFrame: Int64?
    @State private var isHovering = false
    @State private var showEasingPopover = false

    private var centerX: CGFloat {
        trackLeadingInset + timelineX(for: keyframe.frame, pointsPerFrame: pointsPerFrame) - scrollX
    }

    var body: some View {
        // Match segment badge: popover on the fixed-size content, then .position the container.
        // Applying .popover after .position anchors to the filled parent frame instead.
        Image(systemName: "diamond.fill")
            .font(.system(size: isHovering ? 12 : 11))
            .foregroundStyle(isSelected ? Color.accentColor : Color.premnitiplyColor(gray: 1, alpha: 0.68))
            .shadow(color: isHovering ? .black.opacity(0.18) : .clear,
                    radius: 2, y: 1)
            .frame(width: 20, height: propertyRowHeight)
            .contentShape(Rectangle())
            .onHover { isHovering = $0 }
            .gesture(
                DragGesture(minimumDistance: 4)
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
            .onTapGesture {
                showEasingPopover = true
            }
            .popover(isPresented: $showEasingPopover, arrowEdge: .bottom) {
                KeyframeEasingPopover(easing: keyframe.easing,
                                      easingAffectsPlayback: hasOutgoingSegment,
                                      onSetEasing: { easing in
                                          onSetEasing(easing)
                                      },
                                      onDelete: {
                                          showEasingPopover = false
                                          onDelete()
                                      },
                                      onCommit: onEasingCommit,
                                      onDragBegan: onEasingDragBegan,
                                      onDragEnded: onEasingDragEnded)
            }
            .position(x: centerX, y: propertyRowHeight / 2)
    }
}

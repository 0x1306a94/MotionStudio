//
//  TimelinePlayheadViews.swift
//  MotionStudioApp
//
//  Timeline overlay markers and keyframe diamond interactions.
//

import SwiftUI

struct HorizontalSplitDivider: View {
    let width: CGFloat
    let isActive: Bool

    var body: some View {
        ZStack {
            Rectangle()
                .fill(isActive ? Color.accentColor.opacity(0.18) : Color.secondary.opacity(0.14))
            Rectangle()
                .fill(isActive ? Color.accentColor : Color.secondary.opacity(0.45))
                .frame(width: 1)
            Capsule()
                .fill(isActive ? Color.accentColor : Color.secondary.opacity(0.5))
                .frame(width: 3, height: 36)
        }
        .frame(width: width)
    }
}

struct PlayheadLine: View {
    let x: CGFloat
    let isHovering: Bool
    var showsMarker = true

    var body: some View {
        let color = isHovering ? Color.red : Color.blue
        let markerWidth: CGFloat = 10
        ZStack(alignment: .topLeading) {
            Rectangle()
                .fill(color)
                .frame(width: 1)
                .frame(maxHeight: .infinity, alignment: .top)
                .offset(x: x - 0.5)
            if showsMarker {
                PlayheadTriangle()
                    .fill(color)
                    .frame(width: markerWidth, height: 7)
                    .offset(x: x - markerWidth / 2)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

private struct PlayheadTriangle: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}

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
                Button("Ease In") { onSetEasing(.easeIn) }
                Button("Ease Out") { onSetEasing(.easeOut) }
                Button("Hold") { onSetEasing(.hold) }
            }
    }
}

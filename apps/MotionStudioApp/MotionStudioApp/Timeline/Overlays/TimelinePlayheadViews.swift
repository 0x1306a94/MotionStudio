//
//  TimelinePlayheadViews.swift
//  MotionStudioApp
//
//  Timeline overlay markers and split divider views.
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


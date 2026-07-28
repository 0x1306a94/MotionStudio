//
//  CubicBezierPad.swift
//  MotionStudioApp
//
//  Reusable absolute-space cubic Bezier editor (fixed endpoints, draggable
//  control points). Callers map domains (motion-path segment, unit easing
//  square for CUBIC_BEZIER, …) before/after.
//

import SwiftUI

struct CubicBezierPad: View {
    let p0: CGPoint
    let p3: CGPoint
    let c1: CGPoint
    let c2: CGPoint
    let isEditable: Bool
    let onChange: (_ c1: CGPoint, _ c2: CGPoint) -> Void
    let onDragEnded: () -> Void

    @State private var draftC1: CGPoint?
    @State private var draftC2: CGPoint?
    @State private var dragStartContent: CGPoint?

    private var displayC1: CGPoint {
        draftC1 ?? c1
    }

    private var displayC2: CGPoint {
        draftC2 ?? c2
    }

    var body: some View {
        GeometryReader { proxy in
            let layout = Self.layout(size: proxy.size, points: [p0, p3, displayC1, displayC2])
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color(.secondarySystemBackground))
                Path { path in
                    path.move(to: layout.toView(p0))
                    path.addCurve(to: layout.toView(p3),
                                  control1: layout.toView(displayC1),
                                  control2: layout.toView(displayC2))
                }
                .stroke(Color.accentColor, lineWidth: 2)

                handleStem(from: layout.toView(p0), to: layout.toView(displayC1))
                handleStem(from: layout.toView(p3), to: layout.toView(displayC2))

                endpointDot(layout.toView(p0))
                endpointDot(layout.toView(p3))
                controlDot(layout.toView(displayC1),
                           content: displayC1,
                           layout: layout,
                           update: { draftC1 = $0; onChange($0, displayC2) })
                controlDot(layout.toView(displayC2),
                           content: displayC2,
                           layout: layout,
                           update: { draftC2 = $0; onChange(displayC1, $0) })
            }
            .contentShape(Rectangle())
        }
        .frame(height: 160)
        .accessibilityLabel("Bezier curve editor")
    }

    private func handleStem(from: CGPoint, to: CGPoint) -> some View {
        Path { path in
            path.move(to: from)
            path.addLine(to: to)
        }
        .stroke(Color.secondary.opacity(0.7), style: StrokeStyle(lineWidth: 1, dash: [3, 2]))
    }

    private func endpointDot(_ point: CGPoint) -> some View {
        Circle()
            .fill(Color.primary)
            .frame(width: 8, height: 8)
            .position(point)
    }

    private func controlDot(_ viewPoint: CGPoint,
                            content: CGPoint,
                            layout: Layout,
                            update: @escaping (CGPoint) -> Void) -> some View
    {
        Circle()
            .fill(Color.accentColor)
            .frame(width: 14, height: 14)
            .overlay(Circle().stroke(Color.white, lineWidth: 1.5))
            .position(viewPoint)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        guard isEditable else { return }
                        if dragStartContent == nil {
                            dragStartContent = content
                        }
                        guard let start = dragStartContent else { return }
                        let startView = layout.toView(start)
                        let movedView = CGPoint(x: startView.x + value.translation.width,
                                                y: startView.y + value.translation.height)
                        update(layout.toContent(movedView))
                    }
                    .onEnded { _ in
                        guard isEditable else { return }
                        draftC1 = nil
                        draftC2 = nil
                        dragStartContent = nil
                        onDragEnded()
                    },
            )
            .opacity(isEditable ? 1 : 0.45)
    }

    private struct Layout {
        let origin: CGPoint
        let scale: CGFloat

        func toView(_ point: CGPoint) -> CGPoint {
            CGPoint(x: (point.x - origin.x) * scale, y: (point.y - origin.y) * scale)
        }

        func toContent(_ point: CGPoint) -> CGPoint {
            CGPoint(x: point.x / scale + origin.x, y: point.y / scale + origin.y)
        }
    }

    private static func layout(size: CGSize, points: [CGPoint]) -> Layout {
        let padding: CGFloat = 20
        let xs = points.map(\.x)
        let ys = points.map(\.y)
        let minX = xs.min() ?? 0
        let maxX = xs.max() ?? 1
        let minY = ys.min() ?? 0
        let maxY = ys.max() ?? 1
        let width = max(maxX - minX, 1)
        let height = max(maxY - minY, 1)
        let usableWidth = max(size.width - padding * 2, 1)
        let usableHeight = max(size.height - padding * 2, 1)
        let scale = min(usableWidth / width, usableHeight / height)
        let contentWidth = width * scale
        let contentHeight = height * scale
        let origin = CGPoint(
            x: minX - (usableWidth - contentWidth) / (2 * scale) - padding / scale,
            y: minY - (usableHeight - contentHeight) / (2 * scale) - padding / scale,
        )
        return Layout(origin: origin, scale: scale)
    }
}

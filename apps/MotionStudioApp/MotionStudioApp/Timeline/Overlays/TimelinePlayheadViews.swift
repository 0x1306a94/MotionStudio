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

/// Playhead overlays that read the high-frequency PlayheadClock. Reading the
/// clock inside these small views confines per-frame invalidation to the
/// overlays instead of the whole timeline.
struct RulerPlayheadOverlay: View {
    @Environment(PlayheadClock.self) private var clock
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let viewportWidth: CGFloat
    let isHovering: Bool

    var body: some View {
        let visibleX = timelineX(for: clock.frame, pointsPerFrame: pointsPerFrame) - scrollX
        if visibleX >= 0, visibleX <= viewportWidth {
            PlayheadLine(x: trackLeadingInset + visibleX, isHovering: isHovering)
                .allowsHitTesting(false)
        }
    }
}

struct GraphPlayheadOverlay: View {
    @Environment(PlayheadClock.self) private var clock
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let viewportWidth: CGFloat
    @Binding var isHovering: Bool

    var body: some View {
        let visibleX = timelineX(for: clock.frame, pointsPerFrame: pointsPerFrame) - scrollX
        let contentX = trackLeadingInset + visibleX
        Group {
            if visibleX >= 0, visibleX <= viewportWidth {
                PlayheadLine(x: contentX, isHovering: isHovering, showsMarker: false)
                    .allowsHitTesting(false)
                Rectangle()
                    .fill(Color.clear)
                    .contentShape(Rectangle())
                    .frame(width: 20)
                    .frame(maxHeight: .infinity)
                    .offset(x: contentX - 10)
                    .onHover { isHovering = $0 }
                    .gesture(playheadDrag)
            }
        }
    }

    private var playheadDrag: some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("timelineViewport"))
            .onChanged { value in
                clock.publish(timelineFrame(atVisibleX: value.location.x,
                                            pointsPerFrame: pointsPerFrame,
                                            scrollX: scrollX,
                                            duration: duration))
            }
    }
}

struct PlayheadPointerInput: View {
    @Environment(PlayheadClock.self) private var clock
    let editorState: EditorState
    @Binding var isPlayheadHovering: Bool
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let trackWidth: CGFloat
    let viewportWidth: CGFloat

    var body: some View {
        let contentPlayheadX = trackLeadingInset
            + timelineX(for: clock.frame, pointsPerFrame: pointsPerFrame) - scrollX
        TimelinePointerInputView(editorState: editorState,
                                 isPlayheadHovering: $isPlayheadHovering,
                                 duration: duration,
                                 pointsPerFrame: pointsPerFrame,
                                 trackWidth: trackWidth,
                                 viewportWidth: viewportWidth,
                                 visiblePlayheadX: contentPlayheadX,
                                 contentInset: trackLeadingInset)
    }
}

private struct PlayheadTriangle: Shape {
    nonisolated func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}

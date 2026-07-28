//
//  TimelineControls.swift
//  MotionStudioApp
//
//  Playback and preview controls above the timeline.
//

import SwiftUI

struct TimelineControls: View {
    @Bindable var editorState: EditorState
    let duration: Int64

    var body: some View {
        HStack(spacing: 6) {
            Spacer()

            Button {
                editorState.previewBackdrop = editorState.previewBackdrop.next
            } label: {
                Image(systemName: editorState.previewBackdrop.systemImage)
                    .frame(width: 18, height: 18)
            }
            .frame(width: 28, height: 24)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            .accessibilityLabel(editorState.previewBackdrop.accessibilityLabel)
            .help(editorState.previewBackdrop.helpText)

            Button {
                editorState.isPlaying.toggle()
            } label: {
                Image(systemName: editorState.isPlaying ? "pause.fill" : "play.fill")
                    .frame(width: 18, height: 18)
            }
            .frame(width: 28, height: 24)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            // Global space-to-play/pause; a window-level key equivalent so it
            // fires regardless of which panel holds focus (the canvas
            // included). A text field being edited consumes the space first.
            .keyboardShortcut(.space, modifiers: [])

            PlayheadFrameLabel(duration: duration)

            timelineZoomControls

            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .frame(maxWidth: .infinity)
    }

    private var timelineZoomControls: some View {
        HStack(spacing: 4) {
            Button {
                zoomTimeline(by: 1 / timelineZoomStep)
            } label: {
                Image(systemName: "minus.magnifyingglass")
                    .frame(width: 18, height: 18)
            }
            .frame(width: 28, height: 24)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            .accessibilityLabel("Zoom Timeline Out")
            .help("Zoom timeline out")

            Slider(value: $editorState.timelinePointsPerFrame,
                   in: Double(minTimelinePointsPerFrame) ... Double(maxTimelinePointsPerFrame))
                .frame(width: 112)
                .accessibilityLabel("Timeline Zoom")
                .help("Timeline scale")

            Button {
                zoomTimeline(by: timelineZoomStep)
            } label: {
                Image(systemName: "plus.magnifyingglass")
                    .frame(width: 18, height: 18)
            }
            .frame(width: 28, height: 24)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            .accessibilityLabel("Zoom Timeline In")
            .help("Zoom timeline in")
        }
    }

    private func zoomTimeline(by factor: CGFloat) {
        let next = CGFloat(editorState.timelinePointsPerFrame) * factor
        editorState.timelinePointsPerFrame = Double(min(max(next, minTimelinePointsPerFrame),
                                                        maxTimelinePointsPerFrame))
    }
}

private struct PlayheadFrameLabel: View {
    @Environment(PlayheadClock.self) private var clock
    let duration: Int64

    var body: some View {
        Text("\(clock.frame) / \(duration)")
            .monospacedDigit()
            .font(.callout)
            .frame(minWidth: 72, alignment: .leading)
    }
}

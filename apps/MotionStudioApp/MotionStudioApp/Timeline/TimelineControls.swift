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
        ZStack {
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

                Text("\(editorState.playheadFrame) / \(duration)")
                    .monospacedDigit()
                    .font(.callout)
                    .frame(minWidth: 72, alignment: .leading)

                Spacer()
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .frame(maxWidth: .infinity)
    }
}

//
//  TimelineView.swift
//  MotionStudioApp
//
//  Playback controls, frame ruler and the keyframe lane for the selected
//  layer. Keyframe diamonds are draggable; drags merge into one undo step
//  on the core side and register once with the system UndoManager on end.
//

import SwiftUI

struct TimelineView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        let core = document.core
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)

        VStack(spacing: 0) {
            TimelineControls(editorState: editorState, duration: duration)
            Divider()
            TimelineRuler(duration: duration, frameRate: frameRate)
            Divider()
            TimelineKeyframeLane(core: core,
                                 editorState: editorState,
                                 duration: duration,
                                 perform: perform,
                                 registerEdit: registerEdit)
            Spacer(minLength: 0)
        }
    }
}

private let pixelsPerFrame: CGFloat = 6
private let laneHeight: CGFloat = 44
// Keeps the playhead and edge keyframes clear of the window borders, where
// macOS window-resize hot zones would steal the drag.
private let laneGutter: CGFloat = 14

private struct TimelineControls: View {
    @Bindable var editorState: EditorState
    let duration: Int64

    var body: some View {
        HStack(spacing: 12) {
            Button {
                editorState.isPlaying.toggle()
            } label: {
                Image(systemName: editorState.isPlaying ? "pause.fill" : "play.fill")
            }
            Text("\(editorState.playheadFrame) / \(duration)")
                .monospacedDigit()
                .font(.callout)
            Picker("Property", selection: $editorState.timelineProperty) {
                ForEach(TimelineProperty.allCases, id: \.self) { property in
                    Text(property.label).tag(property)
                }
            }
            .pickerStyle(.segmented)
            .frame(maxWidth: 320)
            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }
}

/// Frame ruler with per-second labels.
private struct TimelineRuler: View {
    let duration: Int64
    let frameRate: Double

    var body: some View {
        ScrollView(.horizontal) {
            Canvas { context, _ in
                let total = Int(duration)
                let second = max(Int(frameRate.rounded()), 1)
                let step = max(second / 2, 1)
                for frame in stride(from: 0, through: total, by: step) {
                    let x = CGFloat(frame) * pixelsPerFrame
                    let isSecond = frame % second == 0
                    var tick = Path()
                    tick.move(to: CGPoint(x: x, y: isSecond ? 8 : 14))
                    tick.addLine(to: CGPoint(x: x, y: 22))
                    context.stroke(tick, with: .color(.secondary), lineWidth: 1)
                    if isSecond {
                        context.draw(Text("\(frame / second)s").font(.system(size: 9)),
                                     at: CGPoint(x: x + 10, y: 8), anchor: .leading)
                    }
                }
            }
            .frame(width: CGFloat(duration) * pixelsPerFrame, height: 24)
            .padding(.horizontal, laneGutter)
        }
    }
}

/// Keyframe lane for the selected layer plus the playhead overlay.
private struct TimelineKeyframeLane: View {
    let core: MotionDocumentCore
    let editorState: EditorState
    let duration: Int64
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        let _ = core.revision
        ScrollView(.horizontal) {
            ZStack(alignment: .topLeading) {
                Color.clear
                if let layerID = editorState.selectedLayerID {
                    let keyframes = core.keyframes(entityID: layerID,
                                                   path: editorState.timelineProperty.rawValue)
                    ForEach(keyframes) { keyframe in
                        KeyframeDiamond(keyframe: keyframe,
                                        duration: duration) { from, to in
                            core.moveKeyframe(entityID: layerID,
                                              path: editorState.timelineProperty.rawValue,
                                              from: from, to: to)
                        } onMoveEnded: {
                            core.endDrag()
                            registerEdit("Move Keyframe")
                        } onDelete: {
                            perform("Delete Keyframe") {
                                core.removeKeyframe(entityID: layerID,
                                                    path: editorState.timelineProperty.rawValue,
                                                    frame: keyframe.frame)
                            }
                        } onSetEasing: { easing in
                            perform("Set Easing") {
                                core.setEasing(entityID: layerID,
                                               path: editorState.timelineProperty.rawValue,
                                               frame: keyframe.frame, easing: easing)
                            }
                        }
                    }
                }
                // Playhead
                Rectangle()
                    .fill(.red)
                    .frame(width: 1.5, height: laneHeight)
                    .offset(x: CGFloat(editorState.playheadFrame) * pixelsPerFrame)
                    .allowsHitTesting(false)
            }
            .frame(width: max(CGFloat(duration) * pixelsPerFrame, 1), height: laneHeight)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                        editorState.playheadFrame = min(max(frame, 0), duration)
                    }
            )
            // Applied after the gesture: the gutter stays outside the scrub
            // area while the drag mapping stays lane-relative.
            .padding(.horizontal, laneGutter)
        }
    }
}

/// Draggable keyframe diamond. The drag's translation is measured from the
/// frame captured at drag start so live moves don't drift.
private struct KeyframeDiamond: View {
    let keyframe: KeyframeInfo
    let duration: Int64
    let onMove: (Int64, Int64) -> Void
    let onMoveEnded: () -> Void
    let onDelete: () -> Void
    let onSetEasing: (EasingInfo) -> Void

    @State private var dragStartFrame: Int64?

    var body: some View {
        Image(systemName: "diamond.fill")
            .font(.system(size: 11))
            .foregroundStyle(.yellow)
            .position(x: CGFloat(keyframe.frame) * pixelsPerFrame, y: laneHeight / 2)
            .gesture(
                DragGesture()
                    .onChanged { value in
                        if dragStartFrame == nil {
                            dragStartFrame = keyframe.frame
                        }
                        guard let start = dragStartFrame else { return }
                        let target = Int64(
                            (CGFloat(start) + value.translation.width / pixelsPerFrame).rounded())
                        let clamped = min(max(target, 0), duration)
                        if clamped != keyframe.frame {
                            onMove(keyframe.frame, clamped)
                        }
                    }
                    .onEnded { _ in
                        dragStartFrame = nil
                        onMoveEnded()
                    }
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

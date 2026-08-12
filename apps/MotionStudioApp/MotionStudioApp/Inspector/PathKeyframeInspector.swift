//
//  PathKeyframeInspector.swift
//  MotionStudioApp
//
//  Keyframe diamond for ShapePath.path (enable morph animation at playhead).
//

import SwiftUI

struct PathKeyframeInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    let perform: (String, () -> Void) -> Void

    private let path = ShapeProperty.path.path

    var body: some View {
        let _ = core.panelRevision
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path)
            .contains(playheadFrame)
        HStack {
            Text("Path")
            Spacer()
            Button {
                toggle(hasKeyframe: hasKeyframe)
            } label: {
                Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                    .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                    .id(hasKeyframe)
            }
            .buttonStyle(.plain)
            .disabled(!isEditable)
            .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
        }
        .font(.callout)
        .id("path-kf-\(core.panelRevision)-\(hasKeyframe)")
    }

    private func toggle(hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeBezierPathAtPlayhead(entityID: layerID, path: path,
                                                     frame: playheadFrame)
            }
        }
    }
}

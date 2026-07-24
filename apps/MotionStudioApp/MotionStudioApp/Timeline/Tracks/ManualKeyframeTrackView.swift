//
//  ManualKeyframeTrackView.swift
//  MotionStudioApp
//

import SwiftUI

struct ManualKeyframeTrackView: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let layerID: UInt64
    let path: String
    let duration: Int64
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let isTrackSelected: Bool
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let keyframes = core.keyframes(entityID: layerID, path: path).sorted { $0.frame < $1.frame }
        let segments = zip(keyframes, keyframes.dropFirst()).map(KeyframeSegment.init)

        ZStack(alignment: .topLeading) {
            Color.clear
            ForEach(segments) { segment in
                KeyframeConnectionSegment(layerID: layerID,
                                          path: path,
                                          segment: segment,
                                          pointsPerFrame: pointsPerFrame,
                                          scrollX: scrollX,
                                          isTrackSelected: isTrackSelected)
            }
            ForEach(keyframes) { keyframe in
                KeyframeDiamond(keyframe: keyframe,
                                duration: duration,
                                pointsPerFrame: pointsPerFrame,
                                scrollX: scrollX,
                                isSelected: isKeyframeSelected(keyframe.frame))
                { from, to in
                    core.moveKeyframe(entityID: layerID, path: path, from: from, to: to)
                } onMoveEnded: {
                    core.endDrag()
                    registerEdit("Move Keyframe")
                } onDelete: {
                    perform("Delete Keyframe") {
                        core.removeKeyframe(entityID: layerID, path: path, frame: keyframe.frame)
                    }
                } onSetEasing: { easing in
                    perform("Set Easing") {
                        core.setEasing(entityID: layerID, path: path,
                                       frame: keyframe.frame, easing: easing)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func isKeyframeSelected(_ frame: Int64) -> Bool {
        if isTrackSelected {
            return true
        }
        guard let selection = editorState.selectedTimelineSegment,
              selection.layerID == layerID,
              selection.path == path
        else {
            return false
        }
        return frame == selection.startFrame || frame == selection.endFrame
    }
}

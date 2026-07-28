//
//  PlayheadClock.swift
//  MotionStudioApp
//
//  High-frequency playhead publisher, isolated from EditorState so per-frame
//  playback updates invalidate only the small views that render the playhead
//  instead of the whole editor tree.
//

import Foundation
import Observation

@MainActor
@Observable
final class PlayheadClock {
    private(set) var frame: Int64 = 0

    func publish(_ newFrame: Int64) {
        guard newFrame != frame else { return }
        frame = newFrame
    }
}

//
//  PlayheadClock.swift
//  MotionStudioApp
//
//  High-frequency playhead publisher, isolated from EditorState so per-frame
//  playback updates invalidate only the small views that render the playhead
//  instead of the whole editor tree.
//
//  Timeline (UIKit) must use addListener/removeListener. Inspector may keep
//  reading `frame` via Observation until it is migrated off SwiftUI.
//

import Foundation
import Observation

@MainActor
@Observable
final class PlayheadClock {
    private(set) var frame: Int64 = 0
    @ObservationIgnored private var listeners: [UUID: (Int64) -> Void] = [:]

    func publish(_ newFrame: Int64) {
        guard newFrame != frame else { return }
        frame = newFrame
        for listener in listeners.values {
            listener(newFrame)
        }
    }

    @discardableResult
    func addListener(_ block: @escaping (Int64) -> Void) -> UUID {
        let id = UUID()
        listeners[id] = block
        return id
    }

    func removeListener(_ id: UUID) {
        listeners[id] = nil
    }
}

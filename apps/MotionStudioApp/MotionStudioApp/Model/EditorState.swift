//
//  EditorState.swift
//  MotionStudioApp
//
//  Per-window editor UI state (selection, playhead, playback).
//

import Foundation
import Observation

/// Transient editor state that is not part of the document model.
@MainActor
@Observable
final class EditorState {
    /// Selected layer ID (nil = no selection).
    var selectedLayerID: UInt64?

    /// Playhead position in frames.
    var playheadFrame: Int64 = 0

    var isPlaying = false
}

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

    /// The property currently shown in the timeline keyframe lane.
    var timelineProperty = TimelineProperty.positionX
}

/// Properties editable as timeline keyframe lanes.
enum TimelineProperty: String, CaseIterable, Equatable {
    case positionX = "transform.position"
    case rotation = "transform.rotation"
    case opacity = "transform.opacity"

    var label: String {
        switch self {
        case .positionX:
            return "Position"
        case .rotation:
            return "Rotation"
        case .opacity:
            return "Opacity"
        }
    }
}

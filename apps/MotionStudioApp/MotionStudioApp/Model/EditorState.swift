//
//  EditorState.swift
//  MotionStudioApp
//
//  Per-window editor UI state (selection, playhead, playback).
//

import Foundation
import Observation

struct TimelinePropertySelection: Equatable {
    let layerID: UInt64
    let path: String
}

struct TimelineSegmentSelection: Equatable {
    let layerID: UInt64
    let path: String
    let startFrame: Int64
    let endFrame: Int64
}

enum PreviewBackdrop: Int32 {
    case black = 0
    case transparent = 1

    var next: PreviewBackdrop {
        switch self {
        case .black:
            return .transparent
        case .transparent:
            return .black
        }
    }

    var accessibilityLabel: String {
        switch self {
        case .black:
            return "Switch Preview Backdrop to Transparent"
        case .transparent:
            return "Switch Preview Backdrop to Black"
        }
    }

    var helpText: String {
        switch self {
        case .black:
            return "Preview backdrop: black"
        case .transparent:
            return "Preview backdrop: transparent"
        }
    }

    var systemImage: String {
        switch self {
        case .black:
            return "square.fill"
        case .transparent:
            return "square.grid.2x2"
        }
    }
}

/// Transient editor state that is not part of the document model.
@MainActor
@Observable
final class EditorState {
    /// Selected layer ID (nil = no selection).
    var selectedLayerID: UInt64?

    var selectedTimelineProperty: TimelinePropertySelection?

    var selectedTimelineSegment: TimelineSegmentSelection?

    /// Playhead position in frames.
    var playheadFrame: Int64 = 0

    var isPlaying = false

    var previewBackdrop: PreviewBackdrop = .transparent
}

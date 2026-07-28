//
//  EditorState.swift
//  MotionStudioApp
//
//  Per-window editor UI state (selection, playhead, playback).
//

import Foundation
import MotionStudioBridging
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

/// Transient editor state that is not part of the document model.
@MainActor
@Observable
final class EditorState {
    /// Ordered multi-selection. Last entry is the AE primary selection
    /// (Inspector / anchor handle follow it).
    var selectedLayerIDs: [UInt64] = []

    /// Primary selected layer (last in `selectedLayerIDs`), or nil.
    var selectedLayerID: UInt64? {
        get { selectedLayerIDs.last }
        set {
            if let newValue {
                selectedLayerIDs = [newValue]
            } else {
                selectedLayerIDs = []
            }
        }
    }

    var selectedTimelineProperty: TimelinePropertySelection?

    var selectedTimelineSegment: TimelineSegmentSelection?

    /// Playhead position in frames.
    var playheadFrame: Int64 = 0

    var isPlaying = false

    var previewBackdrop: MS_PREVIEWER_BACKDROP = .TRANSPARENT

    var timelinePointsPerFrame: Double = 6

    var timelineScrollX: Double = 0

    /// Active editor tool (select vs pen path editing).
    var tool: EditorTool = .select

    /// Active path-edit chrome target while `tool == .pen`.
    var pathEditTarget: PathEditTarget?

    /// Selected position keyframe on the motion path (Select tool). `nil` when none.
    var motionPathLayerID: UInt64?
    var motionPathSelectedKeyframe: Int?

    func isLayerSelected(_ layerID: UInt64) -> Bool {
        selectedLayerIDs.contains(layerID)
    }

    /// Selects a layer. When `additive` is true, toggles membership (Shift).
    func selectLayer(_ layerID: UInt64, additive: Bool = false) {
        if additive {
            if let index = selectedLayerIDs.firstIndex(of: layerID) {
                selectedLayerIDs.remove(at: index)
            } else {
                selectedLayerIDs.append(layerID)
            }
        } else {
            selectedLayerIDs = [layerID]
        }
        selectedTimelineProperty = nil
        selectedTimelineSegment = nil
        clearMotionPathSelection()
    }

    func clearLayerSelection() {
        selectedLayerIDs = []
        selectedTimelineProperty = nil
        selectedTimelineSegment = nil
        clearMotionPathSelection()
    }

    func clearMotionPathSelection() {
        motionPathLayerID = nil
        motionPathSelectedKeyframe = nil
    }

    func clearPathEdit() {
        pathEditTarget = nil
        tool = .select
    }
}

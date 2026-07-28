//
//  TimelineSupport.swift
//  MotionStudioApp
//
//  Shared timeline layout constants and row model.
//

import MotionStudioBridging
import SwiftUI

let minTimelinePointsPerFrame: CGFloat = 1
let pixelsPerFrame: CGFloat = 6
let maxTimelinePointsPerFrame: CGFloat = 48
let timelineZoomStep: CGFloat = 1.25
let layerRowHeight: CGFloat = 30
let propertyRowHeight: CGFloat = 24
let rulerHeight: CGFloat = 24
let splitDividerWidth: CGFloat = 5
let trackLeadingInset: CGFloat = 16
let minLayerColumnWidth: CGFloat = 260
let maxLayerColumnWidth: CGFloat = 400
let layerActionIconSize: CGFloat = 16
let layerActionButtonSize: CGFloat = 22
let timelineEndpointHandleWidth: CGFloat = 11
/// Vertical ScrollView viewport for layer-list reorder auto-scroll.
/// Must not collide with graphStrip's "timelineViewport" used by playhead drag.
let timelineLayerListViewportCoordinateSpace = "timelineLayerListViewport"
let layerColumnCoordinateSpace = "layerColumn"
let layerReorderAutoScrollEdge: CGFloat = 36
let layerReorderAutoScrollMaxSpeed: CGFloat = 400

func timelineAnimatedPropertyPaths(core: MotionDocumentCore, layerID: UInt64) -> [String] {
    var paths = TransformProperty.allCases
        .map(\.path)
        .filter { !core.keyframes(entityID: layerID, path: $0).isEmpty }
    if !core.keyframes(entityID: layerID, path: ShapeProperty.size.path).isEmpty {
        paths.append(ShapeProperty.size.path)
    }
    if !core.keyframes(entityID: layerID, path: ShapeProperty.cornerRadius.path).isEmpty {
        paths.append(ShapeProperty.cornerRadius.path)
    }
    if core.hasBezierPath(entityID: layerID, path: "path"),
       !core.keyframes(entityID: layerID, path: "path").isEmpty
    {
        paths.append("path")
    }
    paths.append(contentsOf: timelineStyleTracks(core: core, layerID: layerID).map(\.path))
    paths.append(contentsOf: timelineMaskTracks(core: core, layerID: layerID).map(\.path))
    return paths
}

/// Animated style tracks of a shape layer: fill colors and stroke
/// color/width/trim, fills and strokes numbered like the inspector.
func timelineStyleTracks(core: MotionDocumentCore,
                         layerID: UInt64) -> [(path: String, label: String)]
{
    var fillPosition = 0
    var strokePosition = 0
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.styleCount(layerID: layerID) {
        let styleType = core.styleType(layerID: layerID, index: index)
        var candidates: [(path: String, label: String)] = []
        if styleType == .FILL {
            fillPosition += 1
            candidates = [("styles[\(index)].color", "Fill \(fillPosition) Color")]
        } else if styleType == .STROKE {
            strokePosition += 1
            let name = "Stroke \(strokePosition)"
            candidates = [("styles[\(index)].color", "\(name) Color"),
                          ("styles[\(index)].width", "\(name) Width"),
                          ("styles[\(index)].trimStart", "\(name) Trim Start"),
                          ("styles[\(index)].trimEnd", "\(name) Trim End"),
                          ("styles[\(index)].trimOffset", "\(name) Trim Offset")]
        }
        for candidate in candidates
            where !core.keyframes(entityID: layerID, path: candidate.path).isEmpty
        {
            tracks.append(candidate)
        }
    }
    return tracks
}

/// Animated mask scalar tracks (opacity / feather / expansion).
func timelineMaskTracks(core: MotionDocumentCore,
                        layerID: UInt64) -> [(path: String, label: String)]
{
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.maskCount(layerID: layerID) {
        let name = "Mask \(index + 1)"
        let candidates = [
            ("masks[\(index)].path", "\(name) Path"),
            ("masks[\(index)].opacity", "\(name) Opacity"),
            ("masks[\(index)].feather", "\(name) Feather"),
            ("masks[\(index)].expansion", "\(name) Expansion"),
        ]
        for candidate in candidates
            where !core.keyframes(entityID: layerID, path: candidate.0).isEmpty
        {
            tracks.append((path: candidate.0, label: candidate.1))
        }
    }
    return tracks
}

func timelineUsesManualKeyframeTrack(core: MotionDocumentCore, layerID: UInt64,
                                     path: String) -> Bool
{
    !core.keyframes(entityID: layerID, path: path).isEmpty
}

func timelineX(for frame: Int64, pointsPerFrame: CGFloat = pixelsPerFrame) -> CGFloat {
    CGFloat(frame) * pointsPerFrame
}

func timelineX(for frame: CGFloat, pointsPerFrame: CGFloat = pixelsPerFrame) -> CGFloat {
    frame * pointsPerFrame
}

func buildTimelineRows(core: MotionDocumentCore, layerIDs: [UInt64]) -> [TimelineRow] {
    var rows: [TimelineRow] = []
    for layerID in layerIDs {
        rows.append(TimelineRow(id: .layer(layerID), layerID: layerID, kind: .layer))
        let animatedPaths = Set(timelineAnimatedPropertyPaths(core: core, layerID: layerID))
        for property in TransformProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }
        if animatedPaths.contains(ShapeProperty.size.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: ShapeProperty.size.path,
                                            label: ShapeProperty.size.actionLabel))
        }
        if animatedPaths.contains(ShapeProperty.cornerRadius.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: ShapeProperty.cornerRadius.path,
                                            label: ShapeProperty.cornerRadius.actionLabel))
        }
        if animatedPaths.contains("path") {
            rows.append(timelinePropertyRow(core: core, layerID: layerID, path: "path",
                                            label: "Path"))
        }
        for track in timelineStyleTracks(core: core, layerID: layerID) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: track.path,
                                            label: track.label))
        }
        for track in timelineMaskTracks(core: core, layerID: layerID) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: track.path,
                                            label: track.label))
        }
    }
    return rows
}

private func timelinePropertyRow(core: MotionDocumentCore, layerID: UInt64, path: String,
                                 label: String) -> TimelineRow
{
    if timelineUsesManualKeyframeTrack(core: core, layerID: layerID, path: path) {
        return TimelineRow(id: .keyframeTrack(layerID, path), layerID: layerID,
                           kind: .keyframeTrack(path: path, label: label))
    }
    return TimelineRow(id: .propertySpan(layerID, path), layerID: layerID,
                       kind: .propertySpan(path: path, label: label))
}

/// One row of the flattened timeline model. Both the left tree and the right
/// graph iterate the same array so layer rows and property sub-rows line up
/// vertically for lockstep scrolling.
struct TimelineRow: Identifiable {
    enum RowID: Hashable {
        case layer(UInt64)
        case propertySpan(UInt64, String)
        case keyframeTrack(UInt64, String)
    }

    enum Kind {
        case layer
        case propertySpan(path: String, label: String)
        case keyframeTrack(path: String, label: String)
    }

    let id: RowID
    let layerID: UInt64
    let kind: Kind

    var height: CGFloat {
        switch kind {
        case .layer:
            layerRowHeight
        case .propertySpan, .keyframeTrack:
            propertyRowHeight
        }
    }
}

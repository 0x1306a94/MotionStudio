//
//  TimelineSupport.swift
//  MotionStudioApp
//
//  Shared timeline layout constants and row model.
//

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
let timelineShapeSizePath = ShapeProperty.size.path

func timelineAnimatedPropertyPaths(core: MotionDocumentCore, layerID: UInt64) -> [String] {
    var paths = TransformProperty.allCases
        .map(\.path)
        .filter { !core.keyframes(entityID: layerID, path: $0).isEmpty }
    if !core.keyframes(entityID: layerID, path: timelineShapeSizePath).isEmpty {
        paths.append(timelineShapeSizePath)
    }
    return paths
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
        if animatedPaths.contains(timelineShapeSizePath) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: timelineShapeSizePath,
                                            label: ShapeProperty.size.actionLabel))
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

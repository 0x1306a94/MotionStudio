//
//  TimelineSupport.swift
//  MotionStudioApp
//
//  Shared timeline layout constants and row model.
//

import MotionStudioBridging

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
    if !core.keyframes(entityID: layerID, path: ShapeProperty.position.path).isEmpty {
        paths.append(ShapeProperty.position.path)
    }
    if core.hasBezierPath(entityID: layerID, path: ShapeProperty.path.path),
       !core.keyframes(entityID: layerID, path: ShapeProperty.path.path).isEmpty
    {
        paths.append(ShapeProperty.path.path)
    }
    if !core.keyframes(entityID: layerID, path: ImageProperty.size.path).isEmpty {
        paths.append(ImageProperty.size.path)
    }
    for property in FollowPathProperty.allCases
        where !core.keyframes(entityID: layerID, path: property.path).isEmpty
    {
        paths.append(property.path)
    }
    for property in TextPathProperty.allCases
        where !core.keyframes(entityID: layerID, path: property.path).isEmpty
    {
        paths.append(property.path)
    }
    paths.append(contentsOf: timelineStyleTracks(core: core, layerID: layerID).map(\.path))
    paths.append(contentsOf: timelineMaskTracks(core: core, layerID: layerID).map(\.path))
    paths.append(contentsOf: timelineEffectTracks(core: core, layerID: layerID).map(\.path))
    paths.append(contentsOf: timelineLayerStyleTracks(core: core, layerID: layerID).map(\.path))
    return paths
}

/// Animated style tracks of a shape layer: fill/stroke color (Color mode),
/// gradient geometry/stops (Gradient mode), shader uniforms (Shader mode),
/// and stroke width/trim.
/// Fills and strokes are numbered like the inspector.
func timelineStyleTracks(core: MotionDocumentCore,
                         layerID: UInt64) -> [(path: String, label: String)]
{
    var fillPosition = 0
    var strokePosition = 0
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.styleCount(layerID: layerID) {
        let styleType = core.styleType(layerID: layerID, index: index)
        let paintMode = core.stylePaintMode(layerID: layerID, index: index)
        let isShaderPaint = paintMode == .SHADER
        let isGradientPaint = paintMode == .GRADIENT
        if styleType == .FILL {
            fillPosition += 1
            let name = "Fill \(fillPosition)"
            if isShaderPaint {
                tracks.append(contentsOf: timelineShaderUniformTracks(core: core,
                                                                      layerID: layerID,
                                                                      styleIndex: index,
                                                                      styleLabel: name))
            } else if isGradientPaint {
                tracks.append(contentsOf: timelineGradientTracks(core: core,
                                                                 layerID: layerID,
                                                                 styleIndex: index,
                                                                 styleLabel: name))
            } else {
                let path = StyleProperty.color.path(at: index)
                if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
                    tracks.append((path, "\(name) \(StyleProperty.color.actionLabel)"))
                }
            }
        } else if styleType == .STROKE {
            strokePosition += 1
            let name = "Stroke \(strokePosition)"
            let strokeProperties: [StyleProperty] = (isShaderPaint || isGradientPaint)
                ? [.width, .trimStart, .trimEnd, .trimOffset]
                : StyleProperty.allCases
            for property in strokeProperties {
                let path = property.path(at: index)
                if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
                    tracks.append((path, "\(name) \(property.actionLabel)"))
                }
            }
            if isShaderPaint {
                tracks.append(contentsOf: timelineShaderUniformTracks(core: core,
                                                                      layerID: layerID,
                                                                      styleIndex: index,
                                                                      styleLabel: name))
            } else if isGradientPaint {
                tracks.append(contentsOf: timelineGradientTracks(core: core,
                                                                 layerID: layerID,
                                                                 styleIndex: index,
                                                                 styleLabel: name))
            }
        }
    }
    return tracks
}

/// Animated shader uniform tracks for one Fill/Stroke in Shader paint mode.
func timelineShaderUniformTracks(core: MotionDocumentCore,
                                 layerID: UInt64,
                                 styleIndex: Int,
                                 styleLabel: String) -> [(path: String, label: String)]
{
    let shaderID = core.styleShaderID(layerID: layerID, index: styleIndex)
    guard shaderID != 0 else {
        return []
    }
    var tracks: [(path: String, label: String)] = []
    for uniformIndex in 0 ..< core.shaderUniformCount(shaderID) {
        let uniformName = core.shaderUniformName(shaderID, index: uniformIndex)
        let path = StyleProperty.uniformValue(uniformName, styleIndex: styleIndex)
        if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
            tracks.append((path, "\(styleLabel) \(uniformName)"))
        }
    }
    return tracks
}

/// Animated gradient geometry / stop tracks for one Fill/Stroke in Gradient paint mode.
func timelineGradientTracks(core: MotionDocumentCore,
                            layerID: UInt64,
                            styleIndex: Int,
                            styleLabel: String) -> [(path: String, label: String)]
{
    var candidates: [(path: String, label: String)] = [
        (StyleProperty.gradientStart(styleIndex: styleIndex), "\(styleLabel) Start"),
        (StyleProperty.gradientEnd(styleIndex: styleIndex), "\(styleLabel) End"),
    ]
    let gradientType = core.styleGradientType(layerID: layerID, index: styleIndex)
    if gradientType == .CONIC {
        candidates.append((StyleProperty.gradientStartAngle(styleIndex: styleIndex),
                           "\(styleLabel) Start Angle"))
        candidates.append((StyleProperty.gradientEndAngle(styleIndex: styleIndex),
                           "\(styleLabel) End Angle"))
    }
    let stopCount = core.styleGradientStopCount(layerID: layerID, index: styleIndex)
    for stopIndex in 0 ..< stopCount {
        candidates.append((StyleProperty.gradientStopColor(styleIndex: styleIndex,
                                                           stopIndex: stopIndex),
                           "\(styleLabel) Stop \(stopIndex + 1) Color"))
        candidates.append((StyleProperty.gradientStopPosition(styleIndex: styleIndex,
                                                              stopIndex: stopIndex),
                           "\(styleLabel) Stop \(stopIndex + 1) Pos"))
    }
    return candidates.filter { !core.keyframeFrames(entityID: layerID, path: $0.path).isEmpty }
}

/// Animated mask scalar tracks (opacity / feather / expansion).
func timelineMaskTracks(core: MotionDocumentCore,
                        layerID: UInt64) -> [(path: String, label: String)]
{
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.maskCount(layerID: layerID) {
        let name = "Mask \(index + 1)"
        let candidates = MaskProperty.allCases.map { property in
            (property.path(at: index), "\(name) \(property.actionLabel)")
        }
        for candidate in candidates
            where !core.keyframes(entityID: layerID, path: candidate.0).isEmpty
        {
            tracks.append((path: candidate.0, label: candidate.1))
        }
    }
    return tracks
}

func timelineEffectTracks(core: MotionDocumentCore,
                          layerID: UInt64) -> [(path: String, label: String)]
{
    let layerType = core.layerType(layerID)
    guard layerType == .SHAPE || layerType == .IMAGE || layerType == .TEXT else {
        return []
    }
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.effectCount(layerID: layerID) {
        let type = core.effectType(layerID: layerID, index: index)
        let properties: [EffectProperty]
        switch type {
        case .BRIGHTNESS_CONTRAST:
            properties = [.brightness, .contrast]
        case .GAUSSIAN_BLUR:
            properties = [.blurriness]
        case .INVALID:
            continue
        }
        for property in properties {
            let path = property.path(at: index)
            if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
                tracks.append((path, "\(type.label) \(property.actionLabel)"))
            }
        }
    }
    return tracks
}

func timelineLayerStyleTracks(core: MotionDocumentCore,
                              layerID: UInt64) -> [(path: String, label: String)]
{
    let layerType = core.layerType(layerID)
    guard layerType == .SHAPE || layerType == .IMAGE || layerType == .TEXT else {
        return []
    }
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.layerFxCount(layerID: layerID) {
        let type = core.layerFxType(layerID: layerID, index: index)
        let properties: [LayerStyleFxProperty]
        switch type {
        case .DROP_SHADOW:
            properties = [.color, .opacity, .angle, .distance, .size, .spread]
        case .OUTER_GLOW:
            properties = [.color, .opacity, .size, .spread, .range]
        case .STROKE:
            properties = [.color, .opacity, .size]
        case .INVALID:
            continue
        }
        for property in properties {
            let path = property.path(at: index)
            if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
                tracks.append((path, "\(type.label) \(property.actionLabel)"))
            }
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

/// Last seekable / drawable frame for a composition with `duration` frames
/// (AE half-open range `[0, duration)`).
func timelineLastInclusiveFrame(_ duration: Int64) -> Int64 {
    max(duration - 1, 0)
}

/// Track content width in frame units: markers `0...lastInclusive`, so the
/// playhead on the last frame sits at the track's right edge.
func timelineTrackFrameSpan(_ duration: Int64) -> Int64 {
    timelineLastInclusiveFrame(duration)
}

/// Clamp a UI/playhead frame into the drawable range (defensive if clock is stale).
func timelineEvaluationFrame(_ frame: Int64, duration: Int64) -> Int64 {
    min(max(frame, 0), timelineLastInclusiveFrame(duration))
}

func timelineEvaluationTime(_ time: Double, duration: Int64) -> Double {
    let last = Double(timelineLastInclusiveFrame(duration))
    guard time.isFinite else {
        return 0
    }
    return min(max(time, 0), last)
}

func timelineFrame(atVisibleX visibleX: CGFloat, pointsPerFrame: CGFloat,
                   scrollX: CGFloat, duration: Int64) -> Int64
{
    let frame = Int64(((visibleX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
    return min(max(frame, 0), timelineLastInclusiveFrame(duration))
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

        for property in ShapeProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }

        for property in TextProperty.allCases where animatedPaths.contains(property.path) {
            // fontSize / size are static; only content.text can be animated.
            guard property == .text else { continue }
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }

        for property in FollowPathProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
        }
        for property in TextPathProperty.allCases where animatedPaths.contains(property.path) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: property.path,
                                            label: property.actionLabel))
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
        for track in timelineEffectTracks(core: core, layerID: layerID) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: track.path,
                                            label: track.label))
        }
        for track in timelineLayerStyleTracks(core: core, layerID: layerID) {
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

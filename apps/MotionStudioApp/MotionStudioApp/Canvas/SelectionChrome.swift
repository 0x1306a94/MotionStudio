//
//  SelectionChrome.swift
//  MotionStudioApp
//
//  Bridge wrappers for AE free-transform selection chrome.
//

import CoreGraphics
import Foundation

struct SelectionHandlesSnapshot {
    var valid = false
    var isOriented = false
    var corners: [CGPoint] = Array(repeating: .zero, count: 4)
    var edgeMids: [CGPoint] = Array(repeating: .zero, count: 4)
    var center: CGPoint = .zero
    var anchor: CGPoint = .zero
    var primaryLayerID: UInt64 = 0
    var boxRotationDegrees: CGFloat = 0
    var localMin: CGPoint = .zero
    var localMax: CGPoint = .zero

    init() {}

    init(_ handles: MSSelectionHandles) {
        valid = handles.valid != 0
        isOriented = handles.isOriented != 0
        corners = Self.points(fromX: handles.cornersX, y: handles.cornersY)
        edgeMids = Self.points(fromX: handles.edgeMidsX, y: handles.edgeMidsY)
        center = CGPoint(x: CGFloat(handles.centerX), y: CGFloat(handles.centerY))
        anchor = CGPoint(x: CGFloat(handles.anchorX), y: CGFloat(handles.anchorY))
        primaryLayerID = handles.primaryLayerId
        boxRotationDegrees = CGFloat(handles.boxRotationDegrees)
        localMin = CGPoint(x: CGFloat(handles.localMinX), y: CGFloat(handles.localMinY))
        localMax = CGPoint(x: CGFloat(handles.localMaxX), y: CGFloat(handles.localMaxY))
    }

    var bridgeValue: MSSelectionHandles {
        var handles = MSSelectionHandles()
        handles.valid = valid ? 1 : 0
        handles.isOriented = isOriented ? 1 : 0
        Self.write(points: corners, toX: &handles.cornersX, y: &handles.cornersY)
        Self.write(points: edgeMids, toX: &handles.edgeMidsX, y: &handles.edgeMidsY)
        handles.centerX = Float(center.x)
        handles.centerY = Float(center.y)
        handles.anchorX = Float(anchor.x)
        handles.anchorY = Float(anchor.y)
        handles.primaryLayerId = primaryLayerID
        handles.boxRotationDegrees = Float(boxRotationDegrees)
        handles.localMinX = Float(localMin.x)
        handles.localMinY = Float(localMin.y)
        handles.localMaxX = Float(localMax.x)
        handles.localMaxY = Float(localMax.y)
        return handles
    }

    private static func points(fromX xValues: (Float, Float, Float, Float),
                               y yValues: (Float, Float, Float, Float)) -> [CGPoint]
    {
        [
            CGPoint(x: CGFloat(xValues.0), y: CGFloat(yValues.0)),
            CGPoint(x: CGFloat(xValues.1), y: CGFloat(yValues.1)),
            CGPoint(x: CGFloat(xValues.2), y: CGFloat(yValues.2)),
            CGPoint(x: CGFloat(xValues.3), y: CGFloat(yValues.3)),
        ]
    }

    private static func write(points: [CGPoint],
                              toX xValues: inout (Float, Float, Float, Float),
                              y yValues: inout (Float, Float, Float, Float))
    {
        xValues.0 = Float(points[0].x)
        yValues.0 = Float(points[0].y)
        xValues.1 = Float(points[1].x)
        yValues.1 = Float(points[1].y)
        xValues.2 = Float(points[2].x)
        yValues.2 = Float(points[2].y)
        xValues.3 = Float(points[3].x)
        yValues.3 = Float(points[3].y)
    }
}

enum SelectionHandleHit: Int32 {
    case none = 0
    case anchor = 1
    case scaleCorner0 = 2
    case scaleCorner1 = 3
    case scaleCorner2 = 4
    case scaleCorner3 = 5
    case scaleEdge0 = 6
    case scaleEdge1 = 7
    case scaleEdge2 = 8
    case scaleEdge3 = 9
    case rotate0 = 10
    case rotate1 = 11
    case rotate2 = 12
    case rotate3 = 13

    var isScaleCorner: Bool {
        switch self {
        case .scaleCorner0, .scaleCorner1, .scaleCorner2, .scaleCorner3:
            true
        default:
            false
        }
    }

    var isScaleEdge: Bool {
        switch self {
        case .scaleEdge0, .scaleEdge1, .scaleEdge2, .scaleEdge3:
            true
        default:
            false
        }
    }

    var isRotate: Bool {
        switch self {
        case .rotate0, .rotate1, .rotate2, .rotate3:
            true
        default:
            false
        }
    }

    var cornerIndex: Int? {
        switch self {
        case .scaleCorner0: 0
        case .scaleCorner1: 1
        case .scaleCorner2: 2
        case .scaleCorner3: 3
        default: nil
        }
    }

    var edgeIndex: Int? {
        switch self {
        case .scaleEdge0: 0
        case .scaleEdge1: 1
        case .scaleEdge2: 2
        case .scaleEdge3: 3
        default: nil
        }
    }
}

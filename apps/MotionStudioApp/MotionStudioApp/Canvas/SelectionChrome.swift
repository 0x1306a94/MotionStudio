//
//  SelectionChrome.swift
//  MotionStudioApp
//
//  Bridge wrappers for AE free-transform selection chrome.
//

import CoreGraphics
import Foundation
import MotionStudioBridging

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

    func contains(scenePoint: CGPoint) -> Bool {
        guard valid, corners.count == 4 else {
            return false
        }
        var sign: CGFloat = 0
        for index in 0 ..< 4 {
            let a = corners[index]
            let b = corners[(index + 1) % 4]
            let cross = (b.x - a.x) * (scenePoint.y - a.y) - (b.y - a.y) * (scenePoint.x - a.x)
            if abs(cross) < 1e-6 {
                continue
            }
            if sign == 0 {
                sign = cross
            } else if sign * cross < 0 {
                return false
            }
        }
        return true
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

//
//  PathEditTarget.swift
//  MotionStudioApp
//
//  Pen-tool editing target and editor tool mode.
//

import Foundation
import MotionStudioBridging

enum EditorTool: Equatable {
    case select
    case pen
}

struct PathEditTarget: Equatable {
    var kind: MS_PATH_EDIT
    var layerID: UInt64
    var maskIndex: Int
    /// Index into network.vertices for chrome / legacy index APIs. -1 when none.
    var selectedVertex: Int
    /// Authoring vertex id (0 = none).
    var activeVertexId: UInt32
    /// When true, blank clicks append a vertex and edge from activeVertexId.
    var drawing: Bool

    static func shape(layerID: UInt64, selectedVertex: Int = -1, activeVertexId: UInt32 = 0,
                      drawing: Bool = false) -> PathEditTarget
    {
        PathEditTarget(kind: .SHAPE, layerID: layerID, maskIndex: 0,
                       selectedVertex: selectedVertex, activeVertexId: activeVertexId,
                       drawing: drawing)
    }

    static func mask(layerID: UInt64, maskIndex: Int, selectedVertex: Int = -1,
                     activeVertexId: UInt32 = 0, drawing: Bool = false) -> PathEditTarget
    {
        PathEditTarget(kind: .MASK, layerID: layerID, maskIndex: maskIndex,
                       selectedVertex: selectedVertex, activeVertexId: activeVertexId,
                       drawing: drawing)
    }

    var propertyPath: String {
        switch kind {
        case .MASK:
            MaskProperty.path.path(at: maskIndex)
        default:
            ShapeProperty.path.path
        }
    }

    mutating func selectVertex(id: UInt32, index: Int, drawing: Bool) {
        activeVertexId = id
        selectedVertex = index
        self.drawing = drawing
    }

    mutating func clearVertexSelection() {
        activeVertexId = 0
        selectedVertex = -1
        drawing = false
    }
}

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
    var selectedVertex: Int

    static func shape(layerID: UInt64, selectedVertex: Int = -1) -> PathEditTarget {
        PathEditTarget(kind: .SHAPE, layerID: layerID, maskIndex: 0, selectedVertex: selectedVertex)
    }

    static func mask(layerID: UInt64, maskIndex: Int, selectedVertex: Int = -1) -> PathEditTarget {
        PathEditTarget(kind: .MASK, layerID: layerID, maskIndex: maskIndex, selectedVertex: selectedVertex)
    }

    var propertyPath: String {
        switch kind {
        case .MASK:
            return "masks[\(maskIndex)].path"
        default:
            return "path"
        }
    }
}

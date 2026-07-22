//
//  InspectorProperty.swift
//  MotionStudioApp
//
//  Field metadata used by the inspector UI.
//

import Foundation

enum TransformProperty: String {
    case position = "transform.position"
    case scale = "transform.scale"
    case rotation = "transform.rotation"
    case opacity = "transform.opacity"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .position:
            "Position"
        case .scale:
            "Scale"
        case .rotation:
            "Rotation"
        case .opacity:
            "Opacity"
        }
    }
}

enum TransformField {
    case positionX
    case positionY
    case scaleX
    case scaleY
    case rotation
    case opacity

    var property: TransformProperty {
        switch self {
        case .positionX, .positionY:
            .position
        case .scaleX, .scaleY:
            .scale
        case .rotation:
            .rotation
        case .opacity:
            .opacity
        }
    }

    var label: String {
        switch self {
        case .positionX:
            "Position X"
        case .positionY:
            "Position Y"
        case .scaleX:
            "Scale X"
        case .scaleY:
            "Scale Y"
        case .rotation:
            "Rotation"
        case .opacity:
            "Opacity"
        }
    }
}

enum ShapeSizeField {
    case width
    case height

    var label: String {
        switch self {
        case .width:
            "Width"
        case .height:
            "Height"
        }
    }
}

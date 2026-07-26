//
//  InspectorProperty.swift
//  MotionStudioApp
//
//  Field metadata used by the inspector UI.
//

import Foundation
import SwiftUI
#if canImport(UIKit)
    import UIKit
#endif

/// Blend modes editable on a fill style; raw values mirror the bridge MS_BLEND_* tags.
enum FillBlendMode: Int32, CaseIterable, Identifiable {
    case normal = 0
    case multiply = 1
    case screen = 2
    case overlay = 3
    case darken = 4
    case lighten = 5
    case colorDodge = 6
    case colorBurn = 7
    case hardLight = 8
    case softLight = 9
    case difference = 10
    case exclusion = 11
    case hue = 12
    case saturation = 13
    case color = 14
    case luminosity = 15
    case add = 16

    var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .normal:
            "Normal"
        case .multiply:
            "Multiply"
        case .screen:
            "Screen"
        case .overlay:
            "Overlay"
        case .darken:
            "Darken"
        case .lighten:
            "Lighten"
        case .colorDodge:
            "Color Dodge"
        case .colorBurn:
            "Color Burn"
        case .hardLight:
            "Hard Light"
        case .softLight:
            "Soft Light"
        case .difference:
            "Difference"
        case .exclusion:
            "Exclusion"
        case .hue:
            "Hue"
        case .saturation:
            "Saturation"
        case .color:
            "Color"
        case .luminosity:
            "Luminosity"
        case .add:
            "Add"
        }
    }
}

/// Stroke alignment editable on a stroke style; raw values mirror the bridge
/// MS_STROKE_POSITION_* tags.
enum StrokePositionTag: Int32, CaseIterable, Identifiable {
    case center = 0
    case inside = 1
    case outside = 2

    var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .center:
            "Center"
        case .inside:
            "Inside"
        case .outside:
            "Outside"
        }
    }
}

extension MotionColor {
    init(_ color: Color) {
        #if canImport(UIKit)
            let uiColor = UIColor(color)
            var r: CGFloat = 0
            var g: CGFloat = 0
            var b: CGFloat = 0
            var a: CGFloat = 1
            if uiColor.getRed(&r, green: &g, blue: &b, alpha: &a) {
                self.init(r: Float(r), g: Float(g), b: Float(b), a: Float(a))
            } else {
                self = .black
            }
        #else
            self = .black
        #endif
    }

    var swiftUIColor: Color {
        let clamped = clampedChannels()
        return Color(red: Double(clamped.r),
                     green: Double(clamped.g),
                     blue: Double(clamped.b),
                     opacity: Double(clamped.a))
    }

    func clampedChannels() -> MotionColor {
        MotionColor(r: Self.clampedChannel(r),
                    g: Self.clampedChannel(g),
                    b: Self.clampedChannel(b),
                    a: Self.clampedChannel(a))
    }

    static func clampedChannel(_ value: Float) -> Float {
        min(max(value, 0), 1)
    }
}

enum TransformProperty: String, CaseIterable {
    case anchorPoint = "transform.anchorPoint"
    case position = "transform.position"
    case scale = "transform.scale"
    case rotation = "transform.rotation"
    case opacity = "transform.opacity"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .anchorPoint:
            "Anchor"
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

enum ShapeProperty: String, CaseIterable {
    case size
    case cornerRadius

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .size:
            "Size"
        case .cornerRadius:
            "Corner Radius"
        }
    }
}

enum TransformField {
    case anchorX
    case anchorY
    case positionX
    case positionY
    case scaleX
    case scaleY
    case rotation
    case opacity

    var property: TransformProperty {
        switch self {
        case .anchorX, .anchorY:
            .anchorPoint
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
        case .anchorX:
            "Anchor X"
        case .anchorY:
            "Anchor Y"
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

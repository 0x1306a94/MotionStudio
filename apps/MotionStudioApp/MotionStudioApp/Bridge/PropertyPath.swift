//
//  PropertyPath.swift
//  MotionStudioApp
//
//  Canonical Core property-path strings shared by Inspector, Timeline, and Canvas.
//

import Foundation
import MotionStudioBridging
import SwiftUI

extension MotionColor {
    /// Converts a SwiftUI `Color` (including ColorPicker output with opacity).
    /// Uses `Color.resolve` so gray / Display P3 / catalog colors keep alpha;
    /// `UIColor.getRed` often fails for those and previously fell back to opaque black.
    init(_ color: Color) {
        let resolved = color.resolve(in: EnvironmentValues())
        self.init(r: Float(resolved.red),
                  g: Float(resolved.green),
                  b: Float(resolved.blue),
                  a: Float(resolved.opacity))
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
    case path
    case position
    case size
    case cornerRadius

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .path:
            "Path"
        case .position:
            "Position"
        case .size:
            "Size"
        case .cornerRadius:
            "Corner Radius"
        }
    }
}

enum MaskProperty: String, CaseIterable {
    case path
    case opacity
    case feather
    case expansion

    func path(at index: Int) -> String {
        "masks[\(index)].\(rawValue)"
    }

    var actionLabel: String {
        switch self {
        case .path:
            "Path"
        case .opacity:
            "Opacity"
        case .feather:
            "Feather"
        case .expansion:
            "Expansion"
        }
    }
}

enum StyleProperty: String, CaseIterable {
    case color
    case width
    case trimStart
    case trimEnd
    case trimOffset

    func path(at index: Int) -> String {
        "styles[\(index)].\(rawValue)"
    }

    static func uniformValue(_ name: String, styleIndex: Int) -> String {
        "styles[\(styleIndex)].uniformValues.\(name)"
    }

    static func gradientStart(styleIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.start"
    }

    static func gradientEnd(styleIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.end"
    }

    static func gradientStartAngle(styleIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.startAngle"
    }

    static func gradientEndAngle(styleIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.endAngle"
    }

    static func gradientStopColor(styleIndex: Int, stopIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.stops[\(stopIndex)].color"
    }

    static func gradientStopPosition(styleIndex: Int, stopIndex: Int) -> String {
        "styles[\(styleIndex)].gradient.stops[\(stopIndex)].position"
    }

    var actionLabel: String {
        switch self {
        case .color:
            "Color"
        case .width:
            "Width"
        case .trimStart:
            "Trim Start"
        case .trimEnd:
            "Trim End"
        case .trimOffset:
            "Trim Offset"
        }
    }
}

enum FollowPathProperty: String, CaseIterable {
    case pathOffset = "followPath.pathOffset"
    case orientOffset = "followPath.orientOffset"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .pathOffset:
            "Path Offset"
        case .orientOffset:
            "Orient Offset"
        }
    }
}

enum ImageProperty: String, CaseIterable {
    case size = "image.size"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .size:
            "Size"
        }
    }
}

enum TextProperty: String, CaseIterable {
    case text = "content.text"
    case fontSize = "content.fontSize"
    case size = "content.size"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .text:
            "Text"
        case .fontSize:
            "Font Size"
        case .size:
            "Size"
        }
    }
}

enum TextPathProperty: String, CaseIterable {
    case firstMargin = "content.textPath.firstMargin"
    case lastMargin = "content.textPath.lastMargin"

    var path: String {
        rawValue
    }

    var actionLabel: String {
        switch self {
        case .firstMargin:
            "First Margin"
        case .lastMargin:
            "Last Margin"
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

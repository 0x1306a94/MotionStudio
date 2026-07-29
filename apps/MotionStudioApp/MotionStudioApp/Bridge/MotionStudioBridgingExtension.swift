//
//  MotionStudioBridgingExtension.swift
//  MotionStudio
//
//  Created by KK on 2026/7/27.
//

import MotionStudioBridging

extension MS_BLEND: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_BLEND] {
        [.NORMAL, .MULTIPLY, .SCREEN, .OVERLAY, .DARKEN, .LIGHTEN, .COLOR_DODGE, .COLOR_BURN, .HARD_LIGHT, .SOFT_LIGHT, .DIFFERENCE, .EXCLUSION, .HUE, .SATURATION, .COLOR, .LUMINOSITY, .ADD]
    }

    public var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .INVALID:
            "Invalid"
        case .NORMAL:
            "Normal"
        case .MULTIPLY:
            "Multiply"
        case .SCREEN:
            "Screen"
        case .OVERLAY:
            "Overlay"
        case .DARKEN:
            "Darken"
        case .LIGHTEN:
            "Lighten"
        case .COLOR_DODGE:
            "Color Dodge"
        case .COLOR_BURN:
            "Color Burn"
        case .HARD_LIGHT:
            "Hard Light"
        case .SOFT_LIGHT:
            "Soft Light"
        case .DIFFERENCE:
            "Difference"
        case .EXCLUSION:
            "Exclusion"
        case .HUE:
            "Hue"
        case .SATURATION:
            "Saturation"
        case .COLOR:
            "Color"
        case .LUMINOSITY:
            "Luminosity"
        case .ADD:
            "Add"
        }
    }
}

extension MS_MASK: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_MASK] {
        [.ADD, .SUBTRACT, .INTERSECT]
    }

    public var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .ADD:
            "Add"
        case .SUBTRACT:
            "Subtract"
        case .INTERSECT:
            "Intersect"
        case .INVALID:
            "Invalid"
        }
    }
}

extension MS_TRACK_MATTE: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_TRACK_MATTE] {
        [.NONE, .ALPHA, .ALPHA_INVERTED, .LUMA, .LUMA_INVERTED]
    }

    public var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .NONE:
            "None"
        case .ALPHA:
            "Alpha"
        case .ALPHA_INVERTED:
            "Alpha Inverted"
        case .LUMA:
            "Luma"
        case .LUMA_INVERTED:
            "Luma Inverted"
        }
    }
}

extension MS_STROKE_POSITION: @retroactive CaseIterable, @retroactive Identifiable {
    public static var allCases: [MS_STROKE_POSITION] {
        [
            .CENTER,
            .INSIDE,
            .OUTSIDE,
        ]
    }

    public var id: Int32 {
        rawValue
    }

    var label: String {
        switch self {
        case .INVALID:
            "Invalid"
        case .CENTER:
            "Center"
        case .INSIDE:
            "Inside"
        case .OUTSIDE:
            "Outside"
        }
    }
}

extension MS_SELECTION_HANDLE {
    var isScaleCorner: Bool {
        switch self {
        case .SCALE_CORNER0, .SCALE_CORNER1, .SCALE_CORNER2, .SCALE_CORNER3:
            true
        default:
            false
        }
    }

    var isScaleEdge: Bool {
        switch self {
        case .SCALE_EDGE0, .SCALE_EDGE1, .SCALE_EDGE2, .SCALE_EDGE3:
            true
        default:
            false
        }
    }

    var isRotate: Bool {
        switch self {
        case .ROTATE0, .ROTATE1, .ROTATE2, .ROTATE3:
            true
        default:
            false
        }
    }

    var cornerIndex: Int? {
        switch self {
        case .SCALE_CORNER0: 0
        case .SCALE_CORNER1: 1
        case .SCALE_CORNER2: 2
        case .SCALE_CORNER3: 3
        default: nil
        }
    }

    var edgeIndex: Int? {
        switch self {
        case .SCALE_EDGE0: 0
        case .SCALE_EDGE1: 1
        case .SCALE_EDGE2: 2
        case .SCALE_EDGE3: 3
        default: nil
        }
    }
}

extension MS_PREVIEWER_BACKDROP {
    var next: MS_PREVIEWER_BACKDROP {
        switch self {
        case .BLACK:
            .TRANSPARENT
        case .TRANSPARENT:
            .BLACK
        }
    }

    var accessibilityLabel: String {
        switch self {
        case .BLACK:
            "Switch Preview Backdrop to Transparent"
        case .TRANSPARENT:
            "Switch Preview Backdrop to Black"
        }
    }

    var helpText: String {
        switch self {
        case .BLACK:
            "Preview backdrop: black"
        case .TRANSPARENT:
            "Preview backdrop: transparent"
        }
    }

    var systemImage: String {
        switch self {
        case .BLACK:
            "square.fill"
        case .TRANSPARENT:
            "square.grid.2x2"
        }
    }
}

/// Snapshot of one keyframe for UI display.
struct KeyframeInfo: Equatable, Identifiable {
    let frame: Int64
    let value: Float
    let easing: EasingInfo

    var id: Int64 {
        frame
    }
}

/// Easing curve descriptor mirroring the bridge MS_EASING_* tags.
struct EasingInfo: Equatable {
    var kind: MS_EASING
    var inX: Float = 0
    var inY: Float = 0
    var outX: Float = 1
    var outY: Float = 1

    static let linear = EasingInfo(kind: .LINEAR)
    static let hold = EasingInfo(kind: .HOLD)
    static let ease = EasingInfo(kind: .EASE, inX: 0.25, inY: 0.1, outX: 0.25, outY: 1)
    static let easeIn = EasingInfo(kind: .EASE_IN, inX: 0.42, inY: 0, outX: 1, outY: 1)
    static let easeOut = EasingInfo(kind: .EASE_OUT, inX: 0, inY: 0, outX: 0.58, outY: 1)
    static let easeInOut = EasingInfo(kind: .EASE_IN_OUT, inX: 0.42, inY: 0, outX: 0.58, outY: 1)
}

/// Snapshot of Vec2 keyframe spatial tangents (motion path handles).
struct SpatialTangentsInfo: Equatable {
    var hasIn = false
    var inTangent = CGVector(dx: 0, dy: 0)
    var hasOut = false
    var outTangent = CGVector(dx: 0, dy: 0)
}

struct MotionColor: Equatable {
    var r: Float
    var g: Float
    var b: Float
    var a: Float

    static let black = MotionColor(r: 0, g: 0, b: 0, a: 1)
}

struct CanvasFrameProfile: Equatable {
    let drewFrame: Bool
    let usedFrameCache: Bool
    let layerCount: Int
    let drawCommandCount: Int
    let totalMilliseconds: Double
    let documentLockMilliseconds: Double
    let sceneEvaluateMilliseconds: Double
    let buildCommandsMilliseconds: Double
    let beginFrameMilliseconds: Double
    let playCommandsMilliseconds: Double
    let endFrameMilliseconds: Double
    let endFrameCanvasRestoreMilliseconds: Double
    let endFramePresentMilliseconds: Double
    let endFrameFlushSubmitMilliseconds: Double
    let endFrameDeviceUnlockMilliseconds: Double

    init(_ profile: MSCanvasFrameProfile) {
        drewFrame = profile.drewFrame
        usedFrameCache = profile.usedFrameCache
        layerCount = Int(profile.layerCount)
        drawCommandCount = Int(profile.drawCommandCount)
        totalMilliseconds = profile.totalMs
        documentLockMilliseconds = profile.documentLockMs
        sceneEvaluateMilliseconds = profile.sceneEvaluateMs
        buildCommandsMilliseconds = profile.buildCommandsMs
        beginFrameMilliseconds = profile.beginFrameMs
        playCommandsMilliseconds = profile.playCommandsMs
        endFrameMilliseconds = profile.endFrameMs
        endFrameCanvasRestoreMilliseconds = profile.endFrameCanvasRestoreMs
        endFramePresentMilliseconds = profile.endFramePresentMs
        endFrameFlushSubmitMilliseconds = profile.endFrameFlushSubmitMs
        endFrameDeviceUnlockMilliseconds = profile.endFrameDeviceUnlockMs
    }
}

extension MS_PATH_EDIT: @retroactive Equatable, @retroactive Hashable {}

extension MS_PATH_HANDLE: @retroactive Equatable, @retroactive Hashable {}

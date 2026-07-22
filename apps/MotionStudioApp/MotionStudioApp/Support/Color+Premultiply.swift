//
//  Color+Premultiply.swift
//  MotionStudioApp
//
//  Opaque colors whose RGB components are premultiplied by alpha.
//

import SwiftUI

extension Color {
    static func premnitiplyColor(red: CGFloat, green: CGFloat, blue: CGFloat,
                                 alpha: CGFloat) -> Color
    {
        let clampedAlpha = alpha.clamped(to: 0 ... 1)
        let redComponent = premultipliedComponent(red, alpha: clampedAlpha)
        let greenComponent = premultipliedComponent(green, alpha: clampedAlpha)
        let blueComponent = premultipliedComponent(blue, alpha: clampedAlpha)
        return Color(red: redComponent, green: greenComponent, blue: blueComponent)
    }

    static func premnitiplyColor(gray: CGFloat, alpha: CGFloat) -> Color {
        premnitiplyColor(red: gray, green: gray, blue: gray, alpha: alpha)
    }

    static func premnitiplyColor(red: UInt8, green: UInt8, blue: UInt8,
                                 alpha: UInt8) -> Color
    {
        premnitiplyColor(red: normalizedUInt8(red),
                         green: normalizedUInt8(green),
                         blue: normalizedUInt8(blue),
                         alpha: normalizedUInt8(alpha))
    }

    static func premnitiplyColor(gray: UInt8, alpha: UInt8) -> Color {
        premnitiplyColor(red: gray, green: gray, blue: gray, alpha: alpha)
    }

    static func premnitiplyColor(red: Int8, green: Int8, blue: Int8,
                                 alpha: Int8) -> Color
    {
        premnitiplyColor(red: normalizedInt8(red),
                         green: normalizedInt8(green),
                         blue: normalizedInt8(blue),
                         alpha: normalizedInt8(alpha))
    }

    static func premnitiplyColor(gray: Int8, alpha: Int8) -> Color {
        premnitiplyColor(red: gray, green: gray, blue: gray, alpha: alpha)
    }

    private static func premultipliedComponent(_ component: CGFloat,
                                               alpha: CGFloat) -> CGFloat
    {
        component.clamped(to: 0 ... 1) * alpha
    }

    private static func normalizedUInt8(_ value: UInt8) -> CGFloat {
        CGFloat(value) / CGFloat(UInt8.max)
    }

    private static func normalizedInt8(_ value: Int8) -> CGFloat {
        CGFloat(max(Int(value), 0)) / CGFloat(Int(Int8.max))
    }
}

private extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

//
//  MotionColorTests.swift
//  MotionStudioAppTests
//

@testable import MotionStudio
import SwiftUI
import Testing

@MainActor
struct MotionColorTests {
    @Test
    func `preserves opacity from sRGB Color`() {
        let color = Color(red: 0.2, green: 0.4, blue: 0.6, opacity: 0.35)
        let motion = MotionColor(color).clampedChannels()
        #expect(abs(motion.r - 0.2) < 0.02)
        #expect(abs(motion.g - 0.4) < 0.02)
        #expect(abs(motion.b - 0.6) < 0.02)
        #expect(abs(motion.a - 0.35) < 0.02)
    }

    @Test
    func `preserves opacity from Display P3 Color`() {
        let color = Color(.displayP3, red: 1, green: 0, blue: 0, opacity: 0.5)
        let motion = MotionColor(color).clampedChannels()
        #expect(motion.a > 0.45)
        #expect(motion.a < 0.55)
    }

    @Test
    func `preserves opacity from grayscale Color`() {
        let color = Color(white: 0.5, opacity: 0.25)
        let motion = MotionColor(color).clampedChannels()
        #expect(abs(motion.a - 0.25) < 0.02)
        #expect(abs(motion.r - motion.g) < 0.02)
        #expect(abs(motion.g - motion.b) < 0.02)
    }

    @Test
    func `round trips through swiftUIColor`() {
        let original = MotionColor(r: 0.1, g: 0.2, b: 0.3, a: 0.4)
        let roundTripped = MotionColor(original.swiftUIColor).clampedChannels()
        #expect(abs(roundTripped.r - original.r) < 0.02)
        #expect(abs(roundTripped.g - original.g) < 0.02)
        #expect(abs(roundTripped.b - original.b) < 0.02)
        #expect(abs(roundTripped.a - original.a) < 0.02)
    }
}

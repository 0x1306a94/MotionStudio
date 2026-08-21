//
//  VideoExportOptionsBuilderTests.swift
//  MotionStudioAppTests
//

import CoreGraphics
@testable import MotionStudio
import MotionStudioBridging
import Testing

@MainActor
struct VideoExportOptionsBuilderTests {
    @Test
    func `even floor odd becomes even`() {
        #expect(VideoExportOptionsBuilder.evenFloor(1921) == 1920)
        #expect(VideoExportOptionsBuilder.evenFloor(1080) == 1080)
        #expect(VideoExportOptionsBuilder.evenFloor(1) == 0)
    }

    @Test
    func `default bitrate matches core formula`() {
        let bps = VideoExportOptionsBuilder.defaultBitrateBps(width: 1920, height: 1080, frameRate: 30)
        #expect(bps == 6_220_800)
    }

    @Test
    func `quality scales bitrate and profile`() {
        let size = CGSize(width: 64, height: 64)
        let low = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .low)
        let medium = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .medium)
        let high = VideoExportOptionsBuilder.resolve(size: size, duration: 10, frameRate: 30, quality: .high)
        #expect(low.profile == 1)
        #expect(medium.profile == 2)
        #expect(high.profile == 2)
        #expect(low.bitrateBps <= medium.bitrateBps)
        #expect(high.bitrateBps >= medium.bitrateBps)
        #expect(medium.width == 64)
        #expect(medium.height == 64)
        #expect(medium.container == .MP4)
        #expect(!medium.optimizeForNetworkUse)
    }

    @Test
    func `mp4 keeps network optimize flag`() {
        let size = CGSize(width: 64, height: 64)
        let resolved = VideoExportOptionsBuilder.resolve(size: size,
                                                         duration: 10,
                                                         frameRate: 30,
                                                         quality: .medium,
                                                         container: .MP4,
                                                         optimizeForNetworkUse: true)
        #expect(resolved.container == .MP4)
        #expect(resolved.optimizeForNetworkUse)
    }

    @Test
    func `mov clears network optimize flag`() {
        let size = CGSize(width: 64, height: 64)
        let resolved = VideoExportOptionsBuilder.resolve(size: size,
                                                         duration: 10,
                                                         frameRate: 30,
                                                         quality: .medium,
                                                         container: .MOV,
                                                         optimizeForNetworkUse: true)
        #expect(resolved.container == .MOV)
        #expect(!resolved.optimizeForNetworkUse)
    }
}

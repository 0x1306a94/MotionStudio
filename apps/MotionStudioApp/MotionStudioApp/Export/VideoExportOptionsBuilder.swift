//
//  VideoExportOptionsBuilder.swift
//  MotionStudioApp
//
//  Pure helpers that map UI quality settings to bridge export options.
//

import CoreGraphics
import Foundation
import MotionStudioBridging

enum VideoExportQuality: Int, CaseIterable {
    case low
    case medium
    case high
}

struct VideoExportResolvedSettings: Equatable {
    var width: Int
    var height: Int
    var durationFrames: Int64
    var frameRateNum: Int
    var frameRateDen: Int
    var bitrateBps: Int
    var profile: Int
    var container: MS_VIDEO_CONTAINER
    var optimizeForNetworkUse: Bool
}

enum VideoExportOptionsBuilder {
    nonisolated static func evenFloor(_ value: Int) -> Int {
        max(0, value - (value % 2))
    }

    nonisolated static func defaultBitrateBps(width: Int, height: Int, frameRate: Double) -> Int {
        let raw = Double(width) * Double(height) * frameRate * 0.1
        return Int(min(max(raw, 1_000_000), 50_000_000))
    }

    nonisolated static func resolve(size: CGSize,
                                    duration: Int64,
                                    frameRate: Double,
                                    quality: VideoExportQuality,
                                    container: MS_VIDEO_CONTAINER = .MP4,
                                    optimizeForNetworkUse: Bool = false) -> VideoExportResolvedSettings
    {
        let width = evenFloor(Int(size.width.rounded()))
        let height = evenFloor(Int(size.height.rounded()))
        let fps = frameRate > 0 ? frameRate : 30
        let base = defaultBitrateBps(width: width, height: height, frameRate: fps)
        let bitrate: Int
        let profile: Int
        switch quality {
        case .low:
            bitrate = max(1_000_000, Int(Double(base) * 0.5))
            profile = 1
        case .medium:
            bitrate = base
            profile = 2
        case .high:
            bitrate = min(50_000_000, Int(Double(base) * 2.0))
            profile = 2
        }
        let num = max(1, Int(fps.rounded()))
        let resolvedContainer = container == .MOV ? MS_VIDEO_CONTAINER.MOV : .MP4
        return VideoExportResolvedSettings(width: width,
                                           height: height,
                                           durationFrames: max(0, duration),
                                           frameRateNum: num,
                                           frameRateDen: 1,
                                           bitrateBps: bitrate,
                                           profile: profile,
                                           container: resolvedContainer,
                                           optimizeForNetworkUse: resolvedContainer == .MP4 && optimizeForNetworkUse)
    }
}

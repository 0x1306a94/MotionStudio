//
//  KeyframeSegment.swift
//  MotionStudioApp
//

import Foundation

struct KeyframeSegment: Identifiable {
    let start: KeyframeInfo
    let end: KeyframeInfo

    var id: String {
        "\(start.frame)-\(end.frame)"
    }
}

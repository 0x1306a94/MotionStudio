//
//  TimeRangeDraft.swift
//  MotionStudioApp
//

import Foundation

struct TimeRangeDraft: Equatable {
    let startFrame: Int64
    let endFrame: Int64
    let leadingMaxFrame: Int64
    let trailingMinFrame: Int64

    func frame(for edge: TimeRangeDragEdge) -> Int64 {
        switch edge {
        case .leading:
            startFrame
        case .trailing:
            endFrame
        }
    }
}

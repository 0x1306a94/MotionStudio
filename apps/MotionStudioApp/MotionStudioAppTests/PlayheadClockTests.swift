//
//  PlayheadClockTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct PlayheadClockTests {
    @Test
    func `publish notifies listeners and skips duplicates`() {
        let clock = PlayheadClock()
        var received: [Int64] = []
        let id = clock.addListener { frame in
            received.append(frame)
        }
        clock.publish(3)
        clock.publish(3)
        clock.publish(5)
        clock.removeListener(id)
        clock.publish(7)
        #expect(clock.frame == 7)
        #expect(received == [3, 5])
    }
}

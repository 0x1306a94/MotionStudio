//
//  EasingSegmentBadge.swift
//  MotionStudioApp
//

import MotionStudioBridging
import SwiftUI

struct EasingSegmentBadge: View {
    let easing: EasingInfo
    let isSelected: Bool

    var body: some View {
        Image(systemName: easing.kind == .HOLD ? "pause.fill" : "point.topleft.down.curvedto.point.bottomright.up")
            .font(.system(size: 9, weight: .semibold))
            .foregroundStyle(isSelected ? .white : Color.secondary.opacity(0.7))
            .frame(width: 18, height: 16)
            .background(isSelected ? Color.accentColor : Color.secondary.opacity(0.08),
                        in: RoundedRectangle(cornerRadius: 5))
            .overlay(
                RoundedRectangle(cornerRadius: 5)
                    .stroke(isSelected ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(0.22),
                            lineWidth: 1),
            )
    }
}

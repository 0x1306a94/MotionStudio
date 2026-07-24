//
//  TimeRangeBarBody.swift
//  MotionStudioApp
//

import SwiftUI

struct TimeRangeBarBody: View {
    let isSelected: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: 4)
            .fill(isSelected ? Color.accentColor.opacity(0.9) : Color.secondary.opacity(0.08))
            .overlay(alignment: .leading) {
                timeRangeEndpointHandle
            }
            .overlay(alignment: .trailing) {
                timeRangeEndpointHandle
            }
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? Color.white.opacity(0.65) : Color.secondary.opacity(0.14),
                            lineWidth: 1),
            )
    }

    private var timeRangeEndpointHandle: some View {
        RoundedRectangle(cornerRadius: 1.5)
            .fill(isSelected ? Color.white.opacity(0.9) : Color.secondary.opacity(0.28))
            .frame(width: 2, height: layerRowHeight - 14)
            .padding(.horizontal, 6)
    }
}

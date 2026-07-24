//
//  PropertyTrackBar.swift
//  MotionStudioApp
//

import SwiftUI

struct PropertyTrackBar: View {
    let label: String
    let isSelected: Bool

    var body: some View {
        RoundedRectangle(cornerRadius: 4)
            .fill(isSelected ? Color.accentColor.opacity(0.9) : Color.clear)
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? Color.white.opacity(0.7) : Color.secondary.opacity(0.35),
                            lineWidth: 1),
            )
            .overlay(alignment: .leading) {
                propertyEndpointHandle
            }
            .overlay(alignment: .trailing) {
                propertyEndpointHandle
            }
            .overlay {
                Text(label)
                    .font(.system(size: 9))
                    .fontWeight(.semibold)
                    .foregroundStyle(isSelected ? .white.opacity(0.92) : .secondary)
                    .lineLimit(1)
                    .padding(.horizontal, 4)
            }
    }

    private var propertyEndpointHandle: some View {
        RoundedRectangle(cornerRadius: 1.5)
            .fill(isSelected ? Color.white.opacity(0.9) : Color.secondary.opacity(0.35))
            .frame(width: 2, height: 9)
            .padding(.horizontal, 5)
    }
}

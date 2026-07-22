//
//  LayerEditStatus.swift
//  MotionStudioApp
//

import SwiftUI

struct LayerEditStatus: View {
    let isVisible: Bool
    let isLocked: Bool

    var body: some View {
        if !isVisible || isLocked {
            HStack(spacing: 6) {
                Image(systemName: statusIcon)
                    .font(.system(size: 11, weight: .semibold))
                Text(statusText)
                    .font(.caption)
                    .fontWeight(.semibold)
            }
            .foregroundStyle(.secondary)
            .padding(.horizontal, 8)
            .padding(.vertical, 5)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.secondary.opacity(0.12), in: RoundedRectangle(cornerRadius: 6))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.secondary.opacity(0.22), lineWidth: 1),
            )
        }
    }

    private var statusIcon: String {
        isLocked ? "lock.fill" : "eye.slash"
    }

    private var statusText: String {
        if isLocked && !isVisible {
            return "Locked and Hidden Layer"
        }
        return isLocked ? "Locked Layer" : "Hidden Layer"
    }
}

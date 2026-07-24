//
//  LayerColumnHeader.swift
//  MotionStudioApp
//

import SwiftUI

struct LayerColumnHeader: View {
    var body: some View {
        HStack {
            Text("Layers")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 8)
    }
}

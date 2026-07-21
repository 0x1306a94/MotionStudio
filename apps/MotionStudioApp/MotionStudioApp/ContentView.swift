//
//  ContentView.swift
//  MotionStudioApp
//
//  Created by king on 2026/7/21.
//

import SwiftUI

struct ContentView: View {
    @Binding var document: MotionStudioAppDocument

    var body: some View {
        TextEditor(text: $document.text)
    }
}

#Preview {
    ContentView(document: .constant(MotionStudioAppDocument()))
}

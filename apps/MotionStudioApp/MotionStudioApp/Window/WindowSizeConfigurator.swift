//
//  WindowSizeConfigurator.swift
//  MotionStudioApp
//

#if targetEnvironment(macCatalyst)
import SwiftUI
import UIKit

struct WindowSizeConfigurator: View {
    let minimumWidthRatio: CGFloat
    let minimumHeightRatio: CGFloat

    var body: some View {
        WindowSizeRepresentable(
            minimumWidthRatio: minimumWidthRatio,
            minimumHeightRatio: minimumHeightRatio
        )
    }
}

private struct WindowSizeRepresentable: UIViewRepresentable {
    let minimumWidthRatio: CGFloat
    let minimumHeightRatio: CGFloat

    func makeUIView(context: Context) -> WindowSizeView {
        let view = WindowSizeView()
        view.minimumWidthRatio = minimumWidthRatio
        view.minimumHeightRatio = minimumHeightRatio
        return view
    }

    func updateUIView(_ view: WindowSizeView, context: Context) {
        view.minimumWidthRatio = minimumWidthRatio
        view.minimumHeightRatio = minimumHeightRatio
        view.applyWindowSizing()
    }
}

private final class WindowSizeView: UIView {
    var minimumWidthRatio: CGFloat = 0
    var minimumHeightRatio: CGFloat = 0

    private var canPersistWindowSize = false

    override func didMoveToWindow() {
        super.didMoveToWindow()
        applyWindowSizing()

        DispatchQueue.main.async { [weak self] in
            self?.canPersistWindowSize = true
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        persistCurrentWindowSize()
    }

    func applyWindowSizing() {
        guard let windowScene = window?.windowScene else { return }

        let screenSize = windowScene.screen.bounds.size
        windowScene.sizeRestrictions?.minimumSize = CGSize(
            width: screenSize.width * minimumWidthRatio,
            height: screenSize.height * minimumHeightRatio
        )
    }

    private func persistCurrentWindowSize() {
        guard canPersistWindowSize, let window else { return }

        let size = window.bounds.size
        guard size.width > 0, size.height > 0 else { return }

        WindowSizePersistence.save(size: size)
    }
}

#endif

//
//  WindowSizeConfigurator.swift
//  MotionStudioApp
//

#if targetEnvironment(macCatalyst)
    import SwiftUI
    import UIKit

    struct WindowSizeConfigurator: View {
        var body: some View {
            WindowSizeRepresentable()
        }
    }

    private struct WindowSizeRepresentable: UIViewRepresentable {
        func makeUIView(context _: Context) -> WindowSizeView {
            WindowSizeView()
        }

        func updateUIView(_ view: WindowSizeView, context _: Context) {
            view.applyWindowSizing()
        }
    }

    private final class WindowSizeView: UIView {
        private var canPersistWindowSize = false
        private var initialWindowSize: CGSize?

        override func didMoveToWindow() {
            super.didMoveToWindow()
            applyWindowSizing()

            DispatchQueue.main.async { [weak self] in
                guard let self else {
                    return
                }

                initialWindowSize = window?.bounds.size
                canPersistWindowSize = true
            }
        }

        override func layoutSubviews() {
            super.layoutSubviews()
            persistCurrentWindowSize()
        }

        func applyWindowSizing() {
            guard let windowScene = window?.windowScene else {
                return
            }

            let screenSize = windowScene.screen.bounds.size
            windowScene.sizeRestrictions?.minimumSize = WindowSizeConfiguration.minimumSize(
                for: screenSize,
            )
        }

        private func persistCurrentWindowSize() {
            guard canPersistWindowSize, let window else {
                return
            }

            let size = window.bounds.size
            guard size.width > 0, size.height > 0 else {
                return
            }
            guard !matchesInitialWindowSize(size) else {
                return
            }

            WindowSizePersistence.save(size: size)
        }

        private func matchesInitialWindowSize(_ size: CGSize) -> Bool {
            guard let initialWindowSize else {
                return false
            }

            return abs(size.width - initialWindowSize.width) < 0.5
                && abs(size.height - initialWindowSize.height) < 0.5
        }
    }

#endif

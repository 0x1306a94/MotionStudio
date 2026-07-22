//
//  WindowSizeConfiguration.swift
//  MotionStudioApp
//

#if targetEnvironment(macCatalyst)
    import UIKit

    enum WindowSizeConfiguration {
        private static let preferredDefaultSize = CGSize(width: 1360, height: 880)
        private static let preferredMinimumSize = CGSize(width: 1120, height: 720)
        private static let maximumDefaultScreenRatio = CGSize(width: 0.94, height: 0.92)
        private static let maximumMinimumScreenRatio = CGSize(width: 0.86, height: 0.84)

        static var defaultSize: CGSize {
            let screenSize = UIScreen.main.bounds.size
            let minimumSize = minimumSize(for: screenSize)
            return WindowSizePersistence.persistedSize(
                minimumSize: minimumSize,
                maximumSize: screenSize,
            ) ?? defaultSize(for: screenSize, minimumSize: minimumSize)
        }

        static func minimumSize(for screenSize: CGSize) -> CGSize {
            clamped(
                preferredMinimumSize,
                minimumSize: .zero,
                maximumSize: CGSize(
                    width: screenSize.width * maximumMinimumScreenRatio.width,
                    height: screenSize.height * maximumMinimumScreenRatio.height,
                ),
            )
        }

        private static func defaultSize(for screenSize: CGSize, minimumSize: CGSize) -> CGSize {
            clamped(
                preferredDefaultSize,
                minimumSize: minimumSize,
                maximumSize: CGSize(
                    width: screenSize.width * maximumDefaultScreenRatio.width,
                    height: screenSize.height * maximumDefaultScreenRatio.height,
                ),
            )
        }

        private static func clamped(
            _ size: CGSize,
            minimumSize: CGSize,
            maximumSize: CGSize,
        ) -> CGSize {
            CGSize(
                width: min(max(size.width, minimumSize.width), maximumSize.width),
                height: min(max(size.height, minimumSize.height), maximumSize.height),
            )
        }
    }

    enum WindowSizePersistence {
        static let widthKey = "MotionStudio.windowDefaultWidth"
        static let heightKey = "MotionStudio.windowDefaultHeight"

        static func persistedSize(minimumSize: CGSize, maximumSize: CGSize) -> CGSize? {
            let defaults = UserDefaults.standard
            let width = defaults.double(forKey: widthKey)
            let height = defaults.double(forKey: heightKey)
            guard width > 0, height > 0 else {
                return nil
            }

            return CGSize(
                width: min(max(CGFloat(width), minimumSize.width), maximumSize.width),
                height: min(max(CGFloat(height), minimumSize.height), maximumSize.height),
            )
        }

        static func save(size: CGSize) {
            let defaults = UserDefaults.standard
            defaults.set(Double(size.width), forKey: widthKey)
            defaults.set(Double(size.height), forKey: heightKey)
        }
    }

#endif

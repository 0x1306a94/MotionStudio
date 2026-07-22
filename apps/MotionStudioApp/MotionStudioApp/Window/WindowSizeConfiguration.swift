//
//  WindowSizeConfiguration.swift
//  MotionStudioApp
//

#if targetEnvironment(macCatalyst)
    import UIKit

    enum WindowSizeConfiguration {
        static let defaultWidthRatio: CGFloat = 0.7
        static let defaultHeightRatio: CGFloat = 0.6
        static let minimumWidthRatio: CGFloat = 0.55
        static let minimumHeightRatio: CGFloat = 0.65

        static var defaultSize: CGSize {
            let screenSize = UIScreen.main.bounds.size
            let minimumSize = CGSize(
                width: screenSize.width * minimumWidthRatio,
                height: screenSize.height * minimumHeightRatio,
            )
            return WindowSizePersistence.persistedSize(
                minimumSize: minimumSize,
                maximumSize: screenSize,
            ) ?? CGSize(
                width: screenSize.width * defaultWidthRatio,
                height: screenSize.height * defaultHeightRatio,
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
            guard width > 0, height > 0 else { return nil }

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

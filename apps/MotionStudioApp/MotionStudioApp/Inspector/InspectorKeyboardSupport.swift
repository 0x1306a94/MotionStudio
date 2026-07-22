//
//  InspectorKeyboardSupport.swift
//  MotionStudioApp
//

import SwiftUI
#if canImport(UIKit) && !targetEnvironment(macCatalyst)
    import UIKit
#endif

func inspectorKeyboardOverlap(keyboardFrame: CGRect, in proxy: GeometryProxy) -> CGFloat {
    #if canImport(UIKit) && !targetEnvironment(macCatalyst)
        guard !keyboardFrame.isNull else { return 0 }
        let viewFrame = proxy.frame(in: .global)
        guard keyboardFrame.intersects(viewFrame) else { return 0 }
        return max(0, viewFrame.maxY - keyboardFrame.minY)
    #else
        return 0
    #endif
}

#if canImport(UIKit) && !targetEnvironment(macCatalyst)
    func inspectorKeyboardEndFrame(from notification: Notification) -> CGRect {
        notification.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? CGRect ?? .null
    }
#endif

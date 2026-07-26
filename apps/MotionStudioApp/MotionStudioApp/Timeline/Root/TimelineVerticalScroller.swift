//
//  TimelineVerticalScroller.swift
//  MotionStudioApp
//
//  Bridges SwiftUI ScrollView to UIScrollView for pixel auto-scroll during layer reorder.
//

import SwiftUI
import UIKit

@MainActor
final class TimelineVerticalScroller {
    weak var scrollView: UIScrollView?

    var contentOffsetY: CGFloat {
        scrollView?.contentOffset.y ?? 0
    }

    var viewportHeight: CGFloat {
        scrollView?.bounds.height ?? 0
    }

    func scrollBy(_ deltaY: CGFloat) {
        guard let scrollView, deltaY != 0 else {
            return
        }
        let inset = scrollView.adjustedContentInset
        let minY = -inset.top
        let maxY = max(minY, scrollView.contentSize.height - scrollView.bounds.height + inset.bottom)
        var offset = scrollView.contentOffset
        offset.y = min(max(offset.y + deltaY, minY), maxY)
        scrollView.setContentOffset(offset, animated: false)
    }
}

struct TimelineScrollViewBridge: UIViewRepresentable {
    let scroller: TimelineVerticalScroller

    func makeUIView(context _: Context) -> UIView {
        let view = UIView()
        view.isUserInteractionEnabled = false
        view.backgroundColor = .clear
        return view
    }

    func updateUIView(_ uiView: UIView, context _: Context) {
        DispatchQueue.main.async {
            scroller.scrollView = Self.enclosingScrollView(from: uiView)
        }
    }

    private static func enclosingScrollView(from view: UIView) -> UIScrollView? {
        var current: UIView? = view
        while let candidate = current {
            if let scrollView = candidate as? UIScrollView {
                return scrollView
            }
            current = candidate.superview
        }
        return nil
    }
}

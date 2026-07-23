//
//  EditorSupportingViews.swift
//  MotionStudioApp
//
//  Small support views used by the UIKit editor shell.
//

import SwiftUI
import UIKit

@MainActor
final class TimelineGrabberView: UIView {
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .separator.withAlphaComponent(0.12)
        accessibilityLabel = "Resize Timeline"
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    override func draw(_ rect: CGRect) {
        super.draw(rect)
        let width: CGFloat = 38
        let height: CGFloat = 3
        let grabberRect = CGRect(x: (bounds.width - width) * 0.5,
                                 y: (bounds.height - height) * 0.5,
                                 width: width,
                                 height: height)
        UIColor.secondaryLabel.withAlphaComponent(0.55).setFill()
        UIBezierPath(roundedRect: grabberRect, cornerRadius: height * 0.5).fill()
    }
}

struct UIKitTimelineHostView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void

    var body: some View {
        TimelineView(document: document,
                     editorState: editorState,
                     perform: perform,
                     registerEdit: registerEdit,
                     clearSelection: clearSelection)
    }
}

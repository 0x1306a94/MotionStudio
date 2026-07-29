//
//  TimelineEasingPopoverController.swift
//  MotionStudioApp
//
//  UIKit easing editor popover. Owns mutable easing so preset checkmarks update.
//  Custom curve pad remains a small SwiftUI host (CubicBezierPad).
//

import MotionStudioBridging
import SwiftUI
import UIKit

@MainActor
final class TimelineEasingPopoverController: UIViewController {
    private var currentEasing: EasingInfo
    private let easingAffectsPlayback: Bool
    private let onSetEasing: (EasingInfo) -> Void
    private let onDelete: (() -> Void)?
    private let onCommit: () -> Void
    private let onDragBegan: () -> Void
    private let onDragEnded: () -> Void

    private let stack = UIStackView()
    private let padContainer = UIView()
    private var presetRows: [(easing: EasingInfo, check: UIImageView)] = []
    private let customCheck = UIImageView(image: UIImage(systemName: "checkmark"))
    private var showCustomPad: Bool
    private var padHost: UIHostingController<CubicBezierPad>?
    private var isDraggingPad = false

    init(easing: EasingInfo,
         easingAffectsPlayback: Bool,
         onSetEasing: @escaping (EasingInfo) -> Void,
         onDelete: (() -> Void)?,
         onCommit: @escaping () -> Void,
         onDragBegan: @escaping () -> Void,
         onDragEnded: @escaping () -> Void)
    {
        currentEasing = easing
        self.easingAffectsPlayback = easingAffectsPlayback
        self.onSetEasing = onSetEasing
        self.onDelete = onDelete
        self.onCommit = onCommit
        self.onDragBegan = onDragBegan
        self.onDragEnded = onDragEnded
        showCustomPad = easing.kind == .CUBIC_BEZIER
        super.init(nibName: nil, bundle: nil)
        modalPresentationStyle = .popover
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        stack.axis = .vertical
        stack.alignment = .fill
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)

        let title = UILabel()
        title.text = "Easing"
        title.font = .preferredFont(forTextStyle: .headline)
        stack.addArrangedSubview(title)

        if !easingAffectsPlayback {
            let notice = UILabel()
            notice.font = .preferredFont(forTextStyle: .caption1)
            notice.textColor = .secondaryLabel
            notice.numberOfLines = 0
            notice.text = "No following keyframe — easing is unused until you add one."
            stack.addArrangedSubview(notice)
        }

        let presets: [(String, EasingInfo)] = [
            ("Linear", .linear),
            ("Ease", .ease),
            ("Ease In", .easeIn),
            ("Ease Out", .easeOut),
            ("Ease In Out", .easeInOut),
            ("Hold", .hold),
        ]
        for (titleText, easing) in presets {
            let (row, check) = makeChoiceRow(title: titleText) { [weak self] in
                self?.selectPreset(easing)
            }
            stack.addArrangedSubview(row)
            presetRows.append((easing, check))
        }

        let (customRow, _) = makeChoiceRow(title: "Custom", checkView: customCheck) { [weak self] in
            self?.selectCustom()
        }
        stack.addArrangedSubview(customRow)

        padContainer.translatesAutoresizingMaskIntoConstraints = false
        padContainer.isHidden = true
        padContainer.heightAnchor.constraint(equalToConstant: 160).isActive = true
        stack.addArrangedSubview(padContainer)

        if let onDelete {
            let divider = UIView()
            divider.backgroundColor = .separator
            divider.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale).isActive = true
            stack.addArrangedSubview(divider)
            let delete = UIButton(type: .system)
            delete.setTitle("Delete Keyframe", for: .normal)
            delete.setTitleColor(.systemRed, for: .normal)
            delete.contentHorizontalAlignment = .leading
            delete.addAction(UIAction { _ in onDelete() }, for: .touchUpInside)
            stack.addArrangedSubview(delete)
        }

        // Dim presets when easing does not affect playback; keep title/notice readable.
        for row in presetRows {
            row.check.superview?.alpha = easingAffectsPlayback ? 1 : 0.45
            row.check.superview?.isUserInteractionEnabled = easingAffectsPlayback
        }
        customCheck.superview?.alpha = easingAffectsPlayback ? 1 : 0.45
        customCheck.superview?.isUserInteractionEnabled = easingAffectsPlayback

        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: view.topAnchor, constant: 12),
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 12),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -12),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: view.bottomAnchor, constant: -12),
        ])
        refreshChecks()
        refreshPad()
        updatePreferredSize()
    }

    private func makeChoiceRow(title: String, checkView: UIImageView? = nil,
                               action: @escaping () -> Void) -> (UIView, UIImageView)
    {
        let button = UIButton(type: .system)
        button.contentHorizontalAlignment = .leading
        button.setTitle(title, for: .normal)
        button.setTitleColor(.label, for: .normal)
        button.addAction(UIAction { _ in action() }, for: .touchUpInside)

        let check = checkView ?? UIImageView(image: UIImage(systemName: "checkmark"))
        check.tintColor = .tintColor
        check.setContentHuggingPriority(.required, for: .horizontal)

        let row = UIStackView(arrangedSubviews: [button, check])
        row.axis = .horizontal
        row.alignment = .center
        return (row, check)
    }

    private func selectPreset(_ easing: EasingInfo) {
        showCustomPad = false
        currentEasing = easing
        onSetEasing(easing)
        onCommit()
        refreshChecks()
        refreshPad()
        updatePreferredSize()
    }

    private func selectCustom() {
        showCustomPad = true
        if currentEasing.kind != .CUBIC_BEZIER {
            currentEasing = EasingInfo.custom(inX: 0.25, inY: 0.1, outX: 0.25, outY: 1)
            onSetEasing(currentEasing)
            onCommit()
        }
        refreshChecks()
        refreshPad()
        updatePreferredSize()
    }

    private func refreshChecks() {
        for row in presetRows {
            let selected = !showCustomPad && currentEasing.kind == row.easing.kind && currentEasing.kind != .CUBIC_BEZIER
            row.check.isHidden = !selected
        }
        customCheck.isHidden = !(showCustomPad || currentEasing.kind == .CUBIC_BEZIER)
    }

    private func refreshPad() {
        padHost?.willMove(toParent: nil)
        padHost?.view.removeFromSuperview()
        padHost?.removeFromParent()
        padHost = nil

        let shouldShow = easingAffectsPlayback && (showCustomPad || currentEasing.kind == .CUBIC_BEZIER)
        padContainer.isHidden = !shouldShow
        guard shouldShow else {
            return
        }

        let points = padPoints(from: currentEasing)
        let pad = CubicBezierPad(p0: points.p0, p3: points.p3, c1: points.c1, c2: points.c2, isEditable: true) { [weak self] c1, c2 in
            guard let self else {
                return
            }
            if !isDraggingPad {
                isDraggingPad = true
                onDragBegan()
            }
            currentEasing = Self.easing(fromPadC1: c1, c2: c2)
            onSetEasing(currentEasing)
        } onDragEnded: { [weak self] in
            guard let self, isDraggingPad else {
                return
            }
            isDraggingPad = false
            onDragEnded()
        }
        let host = UIHostingController(rootView: pad)
        host.view.backgroundColor = .clear
        host.view.translatesAutoresizingMaskIntoConstraints = false
        addChild(host)
        padContainer.addSubview(host.view)
        NSLayoutConstraint.activate([
            host.view.topAnchor.constraint(equalTo: padContainer.topAnchor),
            host.view.leadingAnchor.constraint(equalTo: padContainer.leadingAnchor),
            host.view.trailingAnchor.constraint(equalTo: padContainer.trailingAnchor),
            host.view.bottomAnchor.constraint(equalTo: padContainer.bottomAnchor),
        ])
        host.didMove(toParent: self)
        padHost = host
    }

    private func updatePreferredSize() {
        preferredContentSize = CGSize(width: 240, height: padContainer.isHidden ? 290 : 460)
    }

    private func padPoints(from easing: EasingInfo) -> (p0: CGPoint, p3: CGPoint, c1: CGPoint, c2: CGPoint) {
        let inX = CGFloat(max(0, min(1, easing.inX)))
        let outX = CGFloat(max(0, min(1, easing.outX)))
        return (CGPoint(x: 0, y: 1),
                CGPoint(x: 1, y: 0),
                CGPoint(x: inX, y: 1 - CGFloat(easing.inY)),
                CGPoint(x: outX, y: 1 - CGFloat(easing.outY)))
    }

    private static func easing(fromPadC1 c1: CGPoint, c2: CGPoint) -> EasingInfo {
        EasingInfo.custom(inX: Float(max(0, min(1, c1.x))),
                          inY: Float(1 - c1.y),
                          outX: Float(max(0, min(1, c2.x))),
                          outY: Float(1 - c2.y))
    }
}

extension EasingInfo {
    static func custom(inX: Float, inY: Float, outX: Float, outY: Float) -> EasingInfo {
        EasingInfo(kind: .CUBIC_BEZIER, inX: inX, inY: inY, outX: outX, outY: outY)
    }
}

//
//  TimelineControlsView.swift
//  MotionStudioApp
//
//  Playback and preview controls above the UIKit timeline.
//

import UIKit

@MainActor
final class TimelineControlsView: UIView {
    var onZoomChanged: (() -> Void)?

    private let editorState: EditorState
    private let backdropButton = UIButton(type: .system)
    private let playButton = UIButton(type: .system)
    private let frameLabel = UILabel()
    private let zoomOutButton = UIButton(type: .system)
    private let zoomSlider = UISlider()
    private let zoomInButton = UIButton(type: .system)
    private var duration: Int64 = 0
    private var displayedFrame: Int64 = 0
    private var isUpdatingZoomSlider = false

    init(editorState: EditorState) {
        self.editorState = editorState
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        configureChrome()
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func reload(duration: Int64, frame: Int64) {
        self.duration = duration
        displayedFrame = frame
        refreshChrome()
    }

    func setPlayheadFrame(_ frame: Int64) {
        guard displayedFrame != frame else {
            return
        }
        displayedFrame = frame
        frameLabel.text = "\(frame) / \(duration)"
    }

    func refreshPlaybackState() {
        let symbol = editorState.isPlaying ? "pause.fill" : "play.fill"
        playButton.setImage(UIImage(systemName: symbol), for: .normal)
        backdropButton.setImage(UIImage(systemName: editorState.previewBackdrop.systemImage), for: .normal)
        backdropButton.accessibilityLabel = editorState.previewBackdrop.accessibilityLabel
        syncZoomSlider()
    }

    private func configureChrome() {
        let stack = UIStackView(arrangedSubviews: [
            UIView(),
            backdropButton,
            playButton,
            frameLabel,
            zoomOutButton,
            zoomSlider,
            zoomInButton,
            UIView(),
        ])
        stack.axis = .horizontal
        stack.alignment = .center
        stack.spacing = 6
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)

        configureIconButton(backdropButton, action: #selector(cycleBackdrop))
        configureIconButton(playButton, action: #selector(togglePlayback))
        configureIconButton(zoomOutButton, action: #selector(zoomOut), symbol: "minus.magnifyingglass")
        configureIconButton(zoomInButton, action: #selector(zoomIn), symbol: "plus.magnifyingglass")
        zoomOutButton.accessibilityLabel = "Zoom Timeline Out"
        zoomInButton.accessibilityLabel = "Zoom Timeline In"

        frameLabel.font = .preferredFont(forTextStyle: .callout)
        frameLabel.font = UIFont.monospacedDigitSystemFont(ofSize: frameLabel.font.pointSize, weight: .regular)
        frameLabel.setContentHuggingPriority(.required, for: .horizontal)
        frameLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 72).isActive = true

        zoomSlider.minimumValue = Float(minTimelinePointsPerFrame)
        zoomSlider.maximumValue = Float(maxTimelinePointsPerFrame)
        zoomSlider.addTarget(self, action: #selector(zoomSliderChanged), for: .valueChanged)
        zoomSlider.accessibilityLabel = "Timeline Zoom"
        zoomSlider.widthAnchor.constraint(equalToConstant: 112).isActive = true

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 4),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -4),
            heightAnchor.constraint(greaterThanOrEqualToConstant: 32),
        ])
        refreshChrome()
    }

    private func configureIconButton(_ button: UIButton, action: Selector, symbol: String? = nil) {
        button.translatesAutoresizingMaskIntoConstraints = false
        if let symbol {
            button.setImage(UIImage(systemName: symbol), for: .normal)
        }
        button.addTarget(self, action: action, for: .touchUpInside)
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: 28),
            button.heightAnchor.constraint(equalToConstant: 24),
        ])
    }

    private func refreshChrome() {
        frameLabel.text = "\(displayedFrame) / \(duration)"
        refreshPlaybackState()
    }

    private func syncZoomSlider() {
        isUpdatingZoomSlider = true
        zoomSlider.value = Float(editorState.timelinePointsPerFrame)
        isUpdatingZoomSlider = false
    }

    @objc private func cycleBackdrop() {
        editorState.previewBackdrop = editorState.previewBackdrop.next
        refreshPlaybackState()
    }

    @objc private func togglePlayback() {
        editorState.isPlaying.toggle()
        refreshPlaybackState()
    }

    @objc private func zoomOut() {
        applyZoom(factor: 1 / timelineZoomStep)
    }

    @objc private func zoomIn() {
        applyZoom(factor: timelineZoomStep)
    }

    @objc private func zoomSliderChanged() {
        guard !isUpdatingZoomSlider else {
            return
        }
        editorState.timelinePointsPerFrame = Double(zoomSlider.value)
        onZoomChanged?()
    }

    private func applyZoom(factor: CGFloat) {
        let next = CGFloat(editorState.timelinePointsPerFrame) * factor
        editorState.timelinePointsPerFrame = Double(min(max(next, minTimelinePointsPerFrame), maxTimelinePointsPerFrame))
        syncZoomSlider()
        onZoomChanged?()
    }
}

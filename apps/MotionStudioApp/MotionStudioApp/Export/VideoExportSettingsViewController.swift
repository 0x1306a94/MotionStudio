//
//  VideoExportSettingsViewController.swift
//  MotionStudioApp
//
//  Quality settings sheet for MP4 export.
//

import UIKit

@MainActor
final class VideoExportSettingsViewController: UIViewController {
    var quality: VideoExportQuality = .medium
    var onExport: ((VideoExportQuality) -> Void)?
    var onCancel: (() -> Void)?

    private let summary: String
    private let durationFrames: Int64
    private let summaryLabel = UILabel()
    private let qualityControl = UISegmentedControl(items: ["Low", "Medium", "High"])
    private let exportButton = UIButton(type: .system)

    init(summary: String, durationFrames: Int64) {
        self.summary = summary
        self.durationFrames = durationFrames
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        title = "Export MP4"
        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .cancel,
                                                            target: self,
                                                            action: #selector(cancelTapped))

        summaryLabel.text = summary
        summaryLabel.numberOfLines = 0
        summaryLabel.textAlignment = .center
        summaryLabel.font = .preferredFont(forTextStyle: .body)
        summaryLabel.textColor = .secondaryLabel
        summaryLabel.translatesAutoresizingMaskIntoConstraints = false

        qualityControl.selectedSegmentIndex = VideoExportQuality.medium.rawValue
        qualityControl.addTarget(self, action: #selector(qualityChanged), for: .valueChanged)
        qualityControl.translatesAutoresizingMaskIntoConstraints = false

        var exportConfig = UIButton.Configuration.filled()
        exportConfig.title = "Export"
        exportConfig.cornerStyle = .large
        exportButton.configuration = exportConfig
        exportButton.addTarget(self, action: #selector(exportTapped), for: .touchUpInside)
        exportButton.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [summaryLabel, qualityControl, exportButton])
        stack.axis = .vertical
        stack.spacing = 20
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 24),
            qualityControl.heightAnchor.constraint(equalToConstant: 32),
            exportButton.heightAnchor.constraint(equalToConstant: 44),
        ])
    }

    @objc private func qualityChanged() {
        quality = VideoExportQuality(rawValue: qualityControl.selectedSegmentIndex) ?? .medium
    }

    @objc private func cancelTapped() {
        onCancel?()
    }

    @objc private func exportTapped() {
        quality = VideoExportQuality(rawValue: qualityControl.selectedSegmentIndex) ?? .medium
        guard durationFrames > 0 else {
            let alert = UIAlertController(title: "Export Failed",
                                          message: "Composition has no frames to export.",
                                          preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "OK", style: .default))
            present(alert, animated: true)
            return
        }
        onExport?(quality)
    }
}

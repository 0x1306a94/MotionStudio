//
//  VideoExportSettingsViewController.swift
//  MotionStudioApp
//
//  Quality, container, and MP4 network-optimize settings for video export.
//

import MotionStudioBridging
import UIKit

struct VideoExportSettings {
    var quality: VideoExportQuality = .medium
    var container: MS_VIDEO_CONTAINER = .MP4
    var optimizeForNetworkUse: Bool = false
}

@MainActor
final class VideoExportSettingsViewController: UIViewController {
    var onExport: ((VideoExportSettings) -> Void)?
    var onCancel: (() -> Void)?

    private let summary: String
    private let durationFrames: Int64
    private let summaryLabel = UILabel()
    private let formatControl = UISegmentedControl(items: ["MP4", "MOV"])
    private let qualityControl = UISegmentedControl(items: ["Low", "Medium", "High"])
    private let optimizeLabel = UILabel()
    private let optimizeSwitch = UISwitch()
    private let optimizeRow = UIStackView()
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
        title = "Export Video"
        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .cancel,
                                                            target: self,
                                                            action: #selector(cancelTapped))

        summaryLabel.text = summary
        summaryLabel.numberOfLines = 0
        summaryLabel.textAlignment = .center
        summaryLabel.font = .preferredFont(forTextStyle: .body)
        summaryLabel.textColor = .secondaryLabel
        summaryLabel.translatesAutoresizingMaskIntoConstraints = false

        formatControl.selectedSegmentIndex = 0
        formatControl.addTarget(self, action: #selector(formatChanged), for: .valueChanged)
        formatControl.translatesAutoresizingMaskIntoConstraints = false

        qualityControl.selectedSegmentIndex = VideoExportQuality.medium.rawValue
        qualityControl.translatesAutoresizingMaskIntoConstraints = false

        optimizeLabel.text = "Optimize for network"
        optimizeLabel.font = .preferredFont(forTextStyle: .body)
        optimizeLabel.translatesAutoresizingMaskIntoConstraints = false

        optimizeSwitch.isOn = false
        optimizeSwitch.translatesAutoresizingMaskIntoConstraints = false

        optimizeRow.axis = .horizontal
        optimizeRow.alignment = .center
        optimizeRow.distribution = .equalSpacing
        optimizeRow.addArrangedSubview(optimizeLabel)
        optimizeRow.addArrangedSubview(optimizeSwitch)

        var exportConfig = UIButton.Configuration.filled()
        exportConfig.title = "Export"
        exportConfig.cornerStyle = .large
        exportButton.configuration = exportConfig
        exportButton.addTarget(self, action: #selector(exportTapped), for: .touchUpInside)
        exportButton.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [
            summaryLabel, formatControl, qualityControl, optimizeRow, exportButton,
        ])
        stack.axis = .vertical
        stack.spacing = 20
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 24),
            formatControl.heightAnchor.constraint(equalToConstant: 32),
            qualityControl.heightAnchor.constraint(equalToConstant: 32),
            exportButton.heightAnchor.constraint(equalToConstant: 44),
        ])
    }

    @objc private func formatChanged() {
        optimizeRow.isHidden = formatControl.selectedSegmentIndex != 0
    }

    @objc private func cancelTapped() {
        onCancel?()
    }

    @objc private func exportTapped() {
        guard durationFrames > 0 else {
            let alert = UIAlertController(title: "Export Failed",
                                          message: "Composition has no frames to export.",
                                          preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "OK", style: .default))
            present(alert, animated: true)
            return
        }
        let container: MS_VIDEO_CONTAINER = formatControl.selectedSegmentIndex == 1 ? .MOV : .MP4
        let quality = VideoExportQuality(rawValue: qualityControl.selectedSegmentIndex) ?? .medium
        onExport?(VideoExportSettings(quality: quality,
                                      container: container,
                                      optimizeForNetworkUse: container == .MP4 && optimizeSwitch.isOn))
    }
}

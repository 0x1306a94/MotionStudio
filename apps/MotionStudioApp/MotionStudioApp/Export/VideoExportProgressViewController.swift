//
//  VideoExportProgressViewController.swift
//  MotionStudioApp
//
//  Modal progress sheet while encoding MP4.
//

import UIKit

@MainActor
final class VideoExportProgressViewController: UIViewController {
    var onCancel: (() -> Void)?

    private let progressView = UIProgressView(progressViewStyle: .default)
    private let statusLabel = UILabel()
    private let cancelButton = UIButton(type: .system)

    override func viewDidLoad() {
        super.viewDidLoad()
        isModalInPresentation = true
        view.backgroundColor = .systemBackground
        title = "Exporting"

        progressView.translatesAutoresizingMaskIntoConstraints = false
        progressView.progress = 0

        statusLabel.text = "0 / 0"
        statusLabel.textAlignment = .center
        statusLabel.font = .preferredFont(forTextStyle: .body)
        statusLabel.translatesAutoresizingMaskIntoConstraints = false

        var cancelConfig = UIButton.Configuration.plain()
        cancelConfig.title = "Cancel"
        cancelButton.configuration = cancelConfig
        cancelButton.addTarget(self, action: #selector(cancelTapped), for: .touchUpInside)
        cancelButton.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [statusLabel, progressView, cancelButton])
        stack.axis = .vertical
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
            stack.centerYAnchor.constraint(equalTo: view.safeAreaLayoutGuide.centerYAnchor),
            progressView.heightAnchor.constraint(equalToConstant: 4),
        ])
    }

    func update(completed: Int64, total: Int64) {
        statusLabel.text = "\(completed) / \(total)"
        if total > 0 {
            progressView.progress = Float(Double(completed) / Double(total))
        } else {
            progressView.progress = 0
        }
    }

    @objc private func cancelTapped() {
        onCancel?()
    }
}

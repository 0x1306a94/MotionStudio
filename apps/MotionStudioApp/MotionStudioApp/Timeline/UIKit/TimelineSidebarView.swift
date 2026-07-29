//
//  TimelineSidebarView.swift
//  MotionStudioApp
//
//  Layer / property rows for the UIKit timeline sidebar.
//

import MotionStudioBridging
import UIKit

@MainActor
protocol TimelineSidebarViewDelegate: AnyObject {
    func timelineSidebarDidScroll(_ sidebar: TimelineSidebarView, offsetY: CGFloat)
}

@MainActor
final class TimelineSidebarView: UIView {
    weak var delegate: TimelineSidebarViewDelegate?

    private let document: MotionProjectState
    private let editorState: EditorState
    private let playheadClock: PlayheadClock
    private let performEdit: (String, () -> Void) -> Void
    private let registerEdit: (String) -> Void
    private let clearSelection: () -> Void

    private let tableView = UITableView(frame: .zero, style: .plain)
    private let insertionLine = UIView()
    private var rows: [TimelineRow] = []
    private var drag: LayerReorderDragState?
    private var autoScrollTask: Task<Void, Never>?
    private var isSyncingOffset = false

    init(document: MotionProjectState,
         editorState: EditorState,
         playheadClock: PlayheadClock,
         perform: @escaping (String, () -> Void) -> Void,
         registerEdit: @escaping (String) -> Void,
         clearSelection: @escaping () -> Void)
    {
        self.document = document
        self.editorState = editorState
        self.playheadClock = playheadClock
        performEdit = perform
        self.registerEdit = registerEdit
        self.clearSelection = clearSelection
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        configureTable()
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    var contentOffsetY: CGFloat {
        get { tableView.contentOffset.y }
        set {
            guard abs(tableView.contentOffset.y - newValue) > 0.5 else {
                return
            }
            isSyncingOffset = true
            tableView.contentOffset = CGPoint(x: 0, y: newValue)
            isSyncingOffset = false
        }
    }

    func reloadRows(_ rows: [TimelineRow]) {
        self.rows = rows
        tableView.reloadData()
        refreshSelectionAppearance()
        updatePlayheadBadges()
    }

    func refreshSelectionAppearance() {
        for cell in tableView.visibleCells {
            guard let indexPath = tableView.indexPath(for: cell) else {
                continue
            }
            configureSelection(for: cell, row: rows[indexPath.row])
        }
    }

    func updatePlayheadBadges() {
        let frame = playheadClock.frame
        for cell in tableView.visibleCells {
            guard let propertyCell = cell as? TimelinePropertyCell,
                  let indexPath = tableView.indexPath(for: propertyCell)
            else {
                continue
            }
            let row = rows[indexPath.row]
            switch row.kind {
            case let .propertySpan(path, _), let .keyframeTrack(path, _):
                propertyCell.setHasKeyframeAtPlayhead(document.core.keyframes(entityID: row.layerID, path: path)
                    .contains { $0.frame == frame })
            case .layer:
                break
            }
        }
    }

    private func configureTable() {
        tableView.translatesAutoresizingMaskIntoConstraints = false
        tableView.dataSource = self
        tableView.delegate = self
        tableView.separatorStyle = .none
        tableView.backgroundColor = .clear
        tableView.allowsSelection = false
        tableView.showsVerticalScrollIndicator = true
        tableView.register(TimelineLayerCell.self, forCellReuseIdentifier: TimelineLayerCell.reuseID)
        tableView.register(TimelinePropertyCell.self, forCellReuseIdentifier: TimelinePropertyCell.reuseID)
        addSubview(tableView)

        insertionLine.translatesAutoresizingMaskIntoConstraints = false
        insertionLine.backgroundColor = .tintColor
        insertionLine.isHidden = true
        addSubview(insertionLine)

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleBackgroundTap))
        tap.cancelsTouchesInView = false
        tableView.backgroundView = UIView()
        tableView.backgroundView?.addGestureRecognizer(tap)

        NSLayoutConstraint.activate([
            tableView.topAnchor.constraint(equalTo: topAnchor),
            tableView.leadingAnchor.constraint(equalTo: leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: trailingAnchor),
            tableView.bottomAnchor.constraint(equalTo: bottomAnchor),
            insertionLine.leadingAnchor.constraint(equalTo: leadingAnchor),
            insertionLine.trailingAnchor.constraint(equalTo: trailingAnchor),
            insertionLine.heightAnchor.constraint(equalToConstant: 2),
            insertionLine.topAnchor.constraint(equalTo: topAnchor),
        ])
    }

    @objc private func handleBackgroundTap() {
        clearSelection()
    }

    private func configureSelection(for cell: UITableViewCell, row: TimelineRow) {
        switch row.kind {
        case .layer:
            (cell as? TimelineLayerCell)?.setSelectedLayer(editorState.isLayerSelected(row.layerID))
        case let .propertySpan(path, _), let .keyframeTrack(path, _):
            let selected = editorState.selectedTimelineProperty
                == TimelinePropertySelection(layerID: row.layerID, path: path)
            (cell as? TimelinePropertyCell)?.setSelectedProperty(selected)
        }
    }

    private func layerSymbol(_ type: MS_LAYER) -> String {
        switch type {
        case .IMAGE:
            "photo"
        case .TEXT:
            "textformat"
        case .GROUP:
            "circle.dashed"
        case .PRECOMP:
            "film"
        default:
            "square"
        }
    }
}

extension TimelineSidebarView: UITableViewDataSource, UITableViewDelegate {
    func tableView(_: UITableView, numberOfRowsInSection _: Int) -> Int {
        rows.count
    }

    func tableView(_: UITableView, heightForRowAt indexPath: IndexPath) -> CGFloat {
        rows[indexPath.row].height
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let row = rows[indexPath.row]
        let core = document.core
        switch row.kind {
        case .layer:
            let cell = tableView.dequeueReusableCell(withIdentifier: TimelineLayerCell.reuseID, for: indexPath) as! TimelineLayerCell
            let visible = core.layerIsVisible(row.layerID)
            let locked = core.layerIsLocked(row.layerID)
            cell.configure(name: core.layerName(row.layerID),
                           symbolName: layerSymbol(core.layerType(row.layerID)),
                           visible: visible,
                           locked: locked,
                           dimmed: drag?.movingIDs.contains(row.layerID) == true)
            cell.onTap = { [weak self] in
                self?.editorState.selectLayer(row.layerID, additive: KeyboardModifiers.shiftPressed)
                self?.refreshSelectionAppearance()
            }
            cell.onToggleVisible = { [weak self] in
                guard let self else {
                    return
                }
                performEdit(visible ? "Hide Layer" : "Show Layer") {
                    self.document.core.setLayerVisible(row.layerID, visible: !visible)
                }
            }
            cell.onToggleLocked = { [weak self] in
                guard let self else {
                    return
                }
                performEdit(locked ? "Unlock Layer" : "Lock Layer") {
                    self.document.core.setLayerLocked(row.layerID, locked: !locked)
                }
            }
            cell.onReorderChanged = { [weak self] viewportY in
                self?.handleReorderDragChanged(layerID: row.layerID, viewportY: viewportY)
            }
            cell.onReorderEnded = { [weak self] in
                self?.handleReorderDragEnded()
            }
            configureSelection(for: cell, row: row)
            return cell
        case let .propertySpan(path, label), let .keyframeTrack(path, label):
            let cell = tableView.dequeueReusableCell(withIdentifier: TimelinePropertyCell.reuseID, for: indexPath) as! TimelinePropertyCell
            let hasKeyframe = core.keyframes(entityID: row.layerID, path: path)
                .contains { $0.frame == playheadClock.frame }
            cell.configure(label: label,
                           hasKeyframeAtPlayhead: hasKeyframe,
                           dimmed: drag?.movingIDs.contains(row.layerID) == true)
            cell.onTap = { [weak self] in
                guard let self else {
                    return
                }
                editorState.selectedLayerID = row.layerID
                editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
                editorState.selectedTimelineSegment = nil
                refreshSelectionAppearance()
            }
            configureSelection(for: cell, row: row)
            return cell
        }
    }

    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        guard !isSyncingOffset else {
            return
        }
        delegate?.timelineSidebarDidScroll(self, offsetY: scrollView.contentOffset.y)
    }

    func tableView(_: UITableView,
                   contextMenuConfigurationForRowAt indexPath: IndexPath,
                   point _: CGPoint) -> UIContextMenuConfiguration?
    {
        let row = rows[indexPath.row]
        guard case .layer = row.kind else {
            return nil
        }
        let layerID = row.layerID
        return UIContextMenuConfiguration(identifier: nil, previewProvider: nil) { [weak self] _ in
            self?.layerContextMenu(for: layerID)
        }
    }
}

// MARK: - Context menu / reorder

private extension TimelineSidebarView {
    func layerContextMenu(for layerID: UInt64) -> UIMenu {
        let arrangeActions: [(String, LayerArrangeAction)] = [
            ("Bring to Front", .bringToFront),
            ("Bring Forward", .bringForward),
            ("Send Backward", .sendBackward),
            ("Send to Back", .sendToBack),
        ]
        let arrange = arrangeActions.map { title, action in
            UIAction(title: title) { [weak self] _ in
                self?.arrangeFromContextMenu(layerID: layerID, action: action)
            }
        }
        let delete = UIAction(title: "Delete", attributes: .destructive) { [weak self] _ in
            self?.deleteFromContextMenu(layerID: layerID)
        }
        return UIMenu(children: arrange + [delete])
    }

    func arrangeFromContextMenu(layerID: UInt64, action: LayerArrangeAction) {
        if !editorState.isLayerSelected(layerID) {
            editorState.selectLayer(layerID)
        }
        let compositionID = document.core.firstCompositionID
        let current = document.core.layerIDs(compositionID: compositionID)
        let moving = Set(editorState.selectedLayerIDs)
        guard let desired = TimelineReorder.arrangedLayerIDs(current: current, moving: moving, action: action) else {
            return
        }
        performEdit(action.actionName) {
            self.document.core.applyLayerOrder(compositionID: compositionID, desired: desired)
        }
    }

    func deleteFromContextMenu(layerID: UInt64) {
        if !editorState.isLayerSelected(layerID) {
            editorState.selectLayer(layerID)
        }
        let layerIDs = editorState.selectedLayerIDs
        guard !layerIDs.isEmpty else {
            return
        }
        let compositionID = document.core.firstCompositionID
        let deleted = Set(layerIDs)
        let actionName = layerIDs.count > 1 ? "Delete Layers" : "Delete Layer"
        performEdit(actionName) {
            self.document.core.removeLayers(compositionID: compositionID, layerIDs: layerIDs)
            self.editorState.clearLayerSelection()
            if let target = self.editorState.pathEditTarget, deleted.contains(target.layerID) {
                self.editorState.clearPathEdit()
            }
        }
    }

    func handleReorderDragChanged(layerID: UInt64, viewportY: CGFloat) {
        let core = document.core
        let compositionID = core.firstCompositionID
        if drag == nil {
            let moving: Set<UInt64>
            if editorState.isLayerSelected(layerID) {
                moving = Set(editorState.selectedLayerIDs)
            } else {
                editorState.selectLayer(layerID)
                moving = [layerID]
            }
            let startOrder = core.layerIDs(compositionID: compositionID)
            core.beginDrag()
            drag = LayerReorderDragState(movingIDs: moving,
                                         startOrder: startOrder,
                                         lastDesired: startOrder,
                                         frozenFrames: TimelineReorder.layerBlockFrames(rows: rows),
                                         insertionUISlot: nil,
                                         lastViewportY: viewportY,
                                         lastDragLayerID: layerID)
            tableView.reloadData()
        }
        guard var state = drag else {
            return
        }
        state.lastViewportY = viewportY
        state.lastDragLayerID = layerID
        drag = state
        applyReorder(using: state, contentY: contentY(fromViewportY: viewportY))
        updateAutoScroll(viewportY: viewportY)
    }

    func contentY(fromViewportY viewportY: CGFloat) -> CGFloat {
        viewportY + tableView.contentOffset.y + tableView.adjustedContentInset.top
    }

    func applyReorder(using state: LayerReorderDragState, contentY: CGFloat) {
        let compositionID = document.core.firstCompositionID
        let slot = TimelineReorder.uiInsertSlot(y: contentY, frames: state.frozenFrames)
        let insertBefore = TimelineReorder.modelInsertBeforeIndex(uiSlot: slot, layerCount: state.startOrder.count)
        let desired = TimelineReorder.reorderedLayerIDs(current: state.startOrder,
                                                        moving: state.movingIDs,
                                                        insertBeforeModelIndex: insertBefore)
        var next = state
        next.insertionUISlot = slot
        if desired != next.lastDesired {
            document.core.applyLayerOrder(compositionID: compositionID, desired: desired)
            next.lastDesired = desired
        }
        drag = next
        updateInsertionLine(slot: slot, frames: state.frozenFrames)
    }

    func updateInsertionLine(slot: Int?, frames: [LayerBlockFrame]) {
        guard let slot else {
            insertionLine.isHidden = true
            return
        }
        let y: CGFloat = if frames.isEmpty {
            0
        } else if slot <= 0 {
            frames[0].minY
        } else if slot >= frames.count {
            frames[frames.count - 1].maxY
        } else {
            frames[slot].minY
        }
        let contentY = y - tableView.contentOffset.y - tableView.adjustedContentInset.top
        insertionLine.isHidden = false
        insertionLine.frame = CGRect(x: 0, y: contentY - 1, width: bounds.width, height: 2)
    }

    func updateAutoScroll(viewportY: CGFloat) {
        let viewportHeight = max(tableView.bounds.height, 1)
        let edge = layerReorderAutoScrollEdge
        let inEdge = viewportY < edge || viewportY > viewportHeight - edge
        if !inEdge {
            stopAutoScroll()
            return
        }
        startAutoScrollIfNeeded()
    }

    func startAutoScrollIfNeeded() {
        if autoScrollTask != nil {
            return
        }
        autoScrollTask = Task { @MainActor in
            let frameNanoseconds: UInt64 = 16_666_667
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: frameNanoseconds)
                guard !Task.isCancelled, let state = drag else {
                    break
                }
                let viewportHeight = max(tableView.bounds.height, 1)
                let viewportY = state.lastViewportY
                let edge = layerReorderAutoScrollEdge
                let speed: CGFloat
                if viewportY < edge {
                    let intensity = 1 - (viewportY / edge)
                    speed = -layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
                } else if viewportY > viewportHeight - edge {
                    let intensity = (viewportY - (viewportHeight - edge)) / edge
                    speed = layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
                } else {
                    break
                }
                if abs(speed) < 1 {
                    break
                }
                let before = tableView.contentOffset.y
                let maxOffset = max(0, tableView.contentSize.height - tableView.bounds.height
                    + tableView.adjustedContentInset.bottom)
                let next = min(max(before + speed * (1.0 / 60.0), -tableView.adjustedContentInset.top), maxOffset)
                tableView.contentOffset = CGPoint(x: 0, y: next)
                if before == tableView.contentOffset.y {
                    break
                }
                applyReorder(using: state, contentY: contentY(fromViewportY: viewportY))
            }
            autoScrollTask = nil
        }
    }

    func stopAutoScroll() {
        autoScrollTask?.cancel()
        autoScrollTask = nil
    }

    func handleReorderDragEnded() {
        stopAutoScroll()
        guard let state = drag else {
            return
        }
        document.core.endDrag()
        if state.lastDesired != state.startOrder {
            registerEdit(state.movingIDs.count > 1 ? "Move Layers" : "Move Layer")
        }
        drag = nil
        insertionLine.isHidden = true
        // Document observation reloads rows after endDrag mutations settle.
        tableView.reloadData()
    }
}

private struct LayerReorderDragState {
    var movingIDs: Set<UInt64>
    var startOrder: [UInt64]
    var lastDesired: [UInt64]
    var frozenFrames: [LayerBlockFrame]
    var insertionUISlot: Int?
    var lastViewportY: CGFloat
    var lastDragLayerID: UInt64
}

// MARK: - Cells

@MainActor
private final class TimelineLayerCell: UITableViewCell {
    static let reuseID = "TimelineLayerCell"

    var onTap: (() -> Void)?
    var onToggleVisible: (() -> Void)?
    var onToggleLocked: (() -> Void)?
    var onReorderChanged: ((CGFloat) -> Void)?
    var onReorderEnded: (() -> Void)?

    private let iconView = UIImageView()
    private let nameLabel = UILabel()
    private let visibleButton = UIButton(type: .system)
    private let lockedButton = UIButton(type: .system)
    private let selectionBackground = UIView()

    override init(style: UITableViewCell.CellStyle, reuseIdentifier: String?) {
        super.init(style: style, reuseIdentifier: reuseIdentifier)
        backgroundColor = .clear
        contentView.backgroundColor = .clear
        selectionStyle = .none

        selectionBackground.translatesAutoresizingMaskIntoConstraints = false
        selectionBackground.backgroundColor = UIColor.tintColor.withAlphaComponent(0.25)
        selectionBackground.isHidden = true
        contentView.insertSubview(selectionBackground, at: 0)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.tintColor = .secondaryLabel
        iconView.contentMode = .scaleAspectFit

        nameLabel.translatesAutoresizingMaskIntoConstraints = false
        nameLabel.font = .preferredFont(forTextStyle: .callout)
        nameLabel.lineBreakMode = .byTruncatingTail

        configureActionButton(visibleButton, action: #selector(toggleVisible))
        configureActionButton(lockedButton, action: #selector(toggleLocked))

        let stack = UIStackView(arrangedSubviews: [iconView, nameLabel, UIView(), visibleButton, lockedButton])
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.axis = .horizontal
        stack.alignment = .center
        stack.spacing = 6
        contentView.addSubview(stack)

        NSLayoutConstraint.activate([
            selectionBackground.topAnchor.constraint(equalTo: contentView.topAnchor),
            selectionBackground.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            selectionBackground.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            selectionBackground.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
            stack.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 8),
            stack.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -8),
            stack.centerYAnchor.constraint(equalTo: contentView.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 16),
            iconView.heightAnchor.constraint(equalToConstant: 16),
            visibleButton.widthAnchor.constraint(equalToConstant: layerActionButtonSize),
            visibleButton.heightAnchor.constraint(equalToConstant: layerActionButtonSize),
            lockedButton.widthAnchor.constraint(equalToConstant: layerActionButtonSize),
            lockedButton.heightAnchor.constraint(equalToConstant: layerActionButtonSize),
        ])

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
        contentView.addGestureRecognizer(tap)
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handleReorderPan(_:)))
        pan.delegate = self
        contentView.addGestureRecognizer(pan)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func configure(name: String, symbolName: String, visible: Bool, locked: Bool, dimmed: Bool) {
        nameLabel.text = name
        iconView.image = UIImage(systemName: symbolName)
        visibleButton.setImage(UIImage(systemName: visible ? "eye.fill" : "eye.slash"), for: .normal)
        lockedButton.setImage(UIImage(systemName: locked ? "lock.fill" : "lock.open"), for: .normal)
        contentView.alpha = dimmed ? 0.55 : 1
    }

    func setSelectedLayer(_ selected: Bool) {
        selectionBackground.isHidden = !selected
    }

    private func configureActionButton(_ button: UIButton, action: Selector) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.tintColor = .secondaryLabel
        button.addTarget(self, action: action, for: .touchUpInside)
    }

    @objc private func handleTap() {
        onTap?()
    }

    @objc private func toggleVisible() {
        onToggleVisible?()
    }

    @objc private func toggleLocked() {
        onToggleLocked?()
    }

    @objc private func handleReorderPan(_ recognizer: UIPanGestureRecognizer) {
        let y = recognizer.location(in: superview?.superview).y
        switch recognizer.state {
        case .began, .changed:
            // Convert into table viewport coordinates.
            if let table = sequence(first: superview, next: { $0?.superview }).compactMap({ $0 as? UITableView }).first {
                onReorderChanged?(recognizer.location(in: table).y)
            } else {
                onReorderChanged?(y)
            }
        case .ended, .cancelled, .failed:
            onReorderEnded?()
        default:
            break
        }
    }

    override func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
        guard let pan = gestureRecognizer as? UIPanGestureRecognizer else {
            return super.gestureRecognizerShouldBegin(gestureRecognizer)
        }
        let velocity = pan.velocity(in: contentView)
        return abs(velocity.y) >= abs(velocity.x)
    }
}

@MainActor
private final class TimelinePropertyCell: UITableViewCell {
    static let reuseID = "TimelinePropertyCell"

    var onTap: (() -> Void)?

    private let nameLabel = UILabel()
    private let badgeView = UIImageView()
    private let selectionBackground = UIView()

    override init(style: UITableViewCell.CellStyle, reuseIdentifier: String?) {
        super.init(style: style, reuseIdentifier: reuseIdentifier)
        backgroundColor = .clear
        contentView.backgroundColor = .clear
        selectionStyle = .none

        selectionBackground.translatesAutoresizingMaskIntoConstraints = false
        selectionBackground.backgroundColor = UIColor.tintColor.withAlphaComponent(0.08)
        selectionBackground.isHidden = true
        contentView.insertSubview(selectionBackground, at: 0)

        nameLabel.translatesAutoresizingMaskIntoConstraints = false
        nameLabel.font = .preferredFont(forTextStyle: .caption1)
        nameLabel.textColor = .secondaryLabel

        badgeView.translatesAutoresizingMaskIntoConstraints = false
        badgeView.tintColor = .systemYellow
        badgeView.image = UIImage(systemName: "diamond.fill")
        badgeView.isHidden = true
        badgeView.preferredSymbolConfiguration = UIImage.SymbolConfiguration(pointSize: 8)

        contentView.addSubview(nameLabel)
        contentView.addSubview(badgeView)

        NSLayoutConstraint.activate([
            selectionBackground.topAnchor.constraint(equalTo: contentView.topAnchor),
            selectionBackground.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            selectionBackground.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            selectionBackground.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
            nameLabel.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 28),
            nameLabel.centerYAnchor.constraint(equalTo: contentView.centerYAnchor),
            badgeView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -8),
            badgeView.centerYAnchor.constraint(equalTo: contentView.centerYAnchor),
            nameLabel.trailingAnchor.constraint(lessThanOrEqualTo: badgeView.leadingAnchor, constant: -4),
        ])

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
        contentView.addGestureRecognizer(tap)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func configure(label: String, hasKeyframeAtPlayhead: Bool, dimmed: Bool) {
        nameLabel.text = label
        setHasKeyframeAtPlayhead(hasKeyframeAtPlayhead)
        contentView.alpha = dimmed ? 0.55 : 1
    }

    func setSelectedProperty(_ selected: Bool) {
        selectionBackground.isHidden = !selected
    }

    func setHasKeyframeAtPlayhead(_ hasKeyframe: Bool) {
        badgeView.isHidden = !hasKeyframe
    }

    @objc private func handleTap() {
        onTap?()
    }
}

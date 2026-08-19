//
//  TimelineSidebarView.swift
//  MotionStudioApp
//
//  Layer / property rows for the timeline sidebar.
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
    private var rows: [TimelineRow] = []
    private var layerDragContext: LayerDragSessionContext?
    private var didBeginCoreLayerDrag = false
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

    func scrollAnchor() -> TimelineScrollAnchor? {
        tableView.timelineScrollAnchor(rows: rows)
    }

    func reloadRows(_ rows: [TimelineRow], preserving anchor: TimelineScrollAnchor?) {
        self.rows = rows
        guard layerDragContext == nil else {
            return
        }
        isSyncingOffset = true
        tableView.reloadData()
        tableView.restoreTimelineScrollAnchor(anchor, rows: rows)
        isSyncingOffset = false
        refreshSelectionAppearance()
        updatePlayheadBadges()
    }

    func refreshRows(_ rows: [TimelineRow]) {
        self.rows = rows
        for cell in tableView.visibleCells {
            guard let indexPath = tableView.indexPath(for: cell), indexPath.row < rows.count else {
                continue
            }
            configure(cell: cell, row: rows[indexPath.row])
        }
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
        tableView.dragDelegate = self
        tableView.dropDelegate = self
        tableView.dragInteractionEnabled = true
        tableView.separatorStyle = .none
        tableView.backgroundColor = .clear
        tableView.allowsSelection = false
        tableView.showsVerticalScrollIndicator = true
        tableView.register(TimelineLayerCell.self, forCellReuseIdentifier: TimelineLayerCell.reuseID)
        tableView.register(TimelinePropertyCell.self, forCellReuseIdentifier: TimelinePropertyCell.reuseID)
        addSubview(tableView)

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleBackgroundTap))
        tap.cancelsTouchesInView = false
        tableView.backgroundView = UIView()
        tableView.backgroundView?.addGestureRecognizer(tap)

        NSLayoutConstraint.activate([
            tableView.topAnchor.constraint(equalTo: topAnchor),
            tableView.leadingAnchor.constraint(equalTo: leadingAnchor),
            tableView.trailingAnchor.constraint(equalTo: trailingAnchor),
            tableView.bottomAnchor.constraint(equalTo: bottomAnchor),
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

    private func configure(cell: UITableViewCell, row: TimelineRow) {
        let core = document.core
        var parentOf: [UInt64: UInt64] = [:]
        for layerID in core.layerIDs(compositionID: core.firstCompositionID) {
            parentOf[layerID] = core.layerParentID(layerID)
        }
        let depth = TimelineLayerTree.parentDepth(layerID: row.layerID, parentOf: parentOf)
        switch row.kind {
        case .layer:
            guard let layerCell = cell as? TimelineLayerCell else {
                return
            }
            let visible = core.layerIsVisible(row.layerID)
            let locked = core.layerIsLocked(row.layerID)
            layerCell.configure(name: core.layerName(row.layerID),
                                symbolName: layerSymbol(core.layerType(row.layerID)),
                                visible: visible,
                                locked: locked,
                                depth: depth)
            layerCell.onTap = { [weak self] in
                self?.editorState.selectLayer(row.layerID, additive: KeyboardModifiers.shiftPressed)
                self?.refreshSelectionAppearance()
            }
            layerCell.onToggleVisible = { [weak self] in
                guard let self else {
                    return
                }
                performEdit(visible ? "Hide Layer" : "Show Layer") {
                    self.document.core.setLayerVisible(row.layerID, visible: !visible)
                }
            }
            layerCell.onToggleLocked = { [weak self] in
                guard let self else {
                    return
                }
                performEdit(locked ? "Unlock Layer" : "Lock Layer") {
                    self.document.core.setLayerLocked(row.layerID, locked: !locked)
                }
            }
        case let .propertySpan(path, label), let .keyframeTrack(path, label):
            guard let propertyCell = cell as? TimelinePropertyCell else {
                return
            }
            let hasKeyframe = core.keyframes(entityID: row.layerID, path: path)
                .contains { $0.frame == playheadClock.frame }
            propertyCell.configure(label: label, hasKeyframeAtPlayhead: hasKeyframe, depth: depth)
            propertyCell.onTap = { [weak self] in
                guard let self else {
                    return
                }
                editorState.selectedLayerID = row.layerID
                editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
                editorState.selectedTimelineSegment = nil
                refreshSelectionAppearance()
            }
        }
        configureSelection(for: cell, row: row)
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
        let cell: UITableViewCell = switch row.kind {
        case .layer:
            tableView.dequeueReusableCell(withIdentifier: TimelineLayerCell.reuseID, for: indexPath)
        case .propertySpan, .keyframeTrack:
            tableView.dequeueReusableCell(withIdentifier: TimelinePropertyCell.reuseID, for: indexPath)
        }
        configure(cell: cell, row: row)
        return cell
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

// MARK: - Context menu

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
        var parentOf: [UInt64: UInt64] = [:]
        for id in current {
            parentOf[id] = document.core.layerParentID(id)
        }
        let moving = TimelineLayerTree.movingIDsIncludingDescendants(
            order: current, parentOf: parentOf, moving: Set(editorState.selectedLayerIDs),
        )
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

    func finishLayerDragSession() {
        guard let context = layerDragContext else {
            return
        }
        if didBeginCoreLayerDrag {
            document.core.endMergeGroup()
            didBeginCoreLayerDrag = false
        }
        if context.lastDesired != context.startOrder {
            registerEdit(context.movingIDs.count > 1 ? "Move Layers" : "Move Layer")
        }
        layerDragContext = nil
        tableView.reloadData()
        refreshSelectionAppearance()
        updatePlayheadBadges()
    }

    func applyDrop(beforeRow rowIndex: Int, context: LayerDragSessionContext) {
        let slot = TimelineReorder.uiInsertSlot(dropBeforeRow: rowIndex, rows: rows)
        let insertBefore = TimelineReorder.modelInsertBeforeIndex(uiSlot: slot, layerCount: context.startOrder.count)
        let desired = TimelineReorder.reorderedLayerIDs(current: context.startOrder,
                                                        moving: context.movingIDs,
                                                        insertBeforeModelIndex: insertBefore)
        guard desired != context.lastDesired else {
            return
        }
        document.core.applyLayerOrder(compositionID: document.core.firstCompositionID, desired: desired)
        context.lastDesired = desired
    }
}

// MARK: - UITableView drag & drop

extension TimelineSidebarView: UITableViewDragDelegate, UITableViewDropDelegate {
    func tableView(_: UITableView, itemsForBeginning session: UIDragSession, at indexPath: IndexPath) -> [UIDragItem] {
        let row = rows[indexPath.row]
        guard case .layer = row.kind else {
            return []
        }
        let layerID = row.layerID
        if !editorState.isLayerSelected(layerID) {
            editorState.selectLayer(layerID)
        }
        let startOrder = document.core.layerIDs(compositionID: document.core.firstCompositionID)
        var parentOf: [UInt64: UInt64] = [:]
        for id in startOrder {
            parentOf[id] = document.core.layerParentID(id)
        }
        let moving = TimelineLayerTree.movingIDsIncludingDescendants(
            order: startOrder, parentOf: parentOf, moving: Set(editorState.selectedLayerIDs),
        )
        session.localContext = LayerDragSessionContext(movingIDs: moving,
                                                       startOrder: startOrder,
                                                       lastDesired: startOrder)

        let item = UIDragItem(itemProvider: NSItemProvider(object: "\(layerID)" as NSString))
        item.localObject = layerID
        return [item]
    }

    func tableView(_: UITableView, dragSessionWillBegin session: UIDragSession) {
        layerDragContext = session.localContext as? LayerDragSessionContext
        document.core.beginMergeGroup()
        didBeginCoreLayerDrag = true
    }

    func tableView(_: UITableView, dragSessionDidEnd session: UIDragSession) {
        if layerDragContext == nil {
            layerDragContext = session.localContext as? LayerDragSessionContext
        }
        finishLayerDragSession()
    }

    func tableView(_: UITableView,
                   dropSessionDidUpdate session: UIDropSession,
                   withDestinationIndexPath _: IndexPath?) -> UITableViewDropProposal
    {
        guard session.localDragSession != nil else {
            return UITableViewDropProposal(operation: .cancel)
        }
        return UITableViewDropProposal(operation: .move, intent: .insertAtDestinationIndexPath)
    }

    func tableView(_: UITableView, performDropWith coordinator: UITableViewDropCoordinator) {
        guard let context = (coordinator.session.localDragSession?.localContext as? LayerDragSessionContext)
            ?? layerDragContext
        else {
            return
        }
        // Apply model order only — avoid UITableView row animations that break property sub-rows.
        let destinationRow = coordinator.destinationIndexPath?.row ?? rows.count
        applyDrop(beforeRow: destinationRow, context: context)
    }

    func tableView(_: UITableView, canHandle session: UIDropSession) -> Bool {
        session.localDragSession != nil
    }

    func tableView(_: UITableView,
                   dragPreviewParametersForRowAt indexPath: IndexPath) -> UIDragPreviewParameters?
    {
        guard case .layer = rows[indexPath.row].kind else {
            return nil
        }
        let parameters = UIDragPreviewParameters()
        parameters.backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.92)
        return parameters
    }
}

private final class LayerDragSessionContext: NSObject {
    let movingIDs: Set<UInt64>
    let startOrder: [UInt64]
    var lastDesired: [UInt64]

    init(movingIDs: Set<UInt64>, startOrder: [UInt64], lastDesired: [UInt64]) {
        self.movingIDs = movingIDs
        self.startOrder = startOrder
        self.lastDesired = lastDesired
    }
}

// MARK: - Cells

@MainActor
private final class TimelineLayerCell: UITableViewCell {
    static let reuseID = "TimelineLayerCell"

    var onTap: (() -> Void)?
    var onToggleVisible: (() -> Void)?
    var onToggleLocked: (() -> Void)?

    private let iconView = UIImageView()
    private let nameLabel = UILabel()
    private let visibleButton = UIButton(type: .system)
    private let lockedButton = UIButton(type: .system)
    private let selectionBackground = UIView()
    private var stackLeadingConstraint: NSLayoutConstraint?

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

        let leading = stack.leadingAnchor.constraint(equalTo: contentView.leadingAnchor,
                                                     constant: TimelineLayerTree.layerLeading)
        stackLeadingConstraint = leading
        NSLayoutConstraint.activate([
            selectionBackground.topAnchor.constraint(equalTo: contentView.topAnchor),
            selectionBackground.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            selectionBackground.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            selectionBackground.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
            leading,
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
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func configure(name: String, symbolName: String, visible: Bool, locked: Bool, depth: Int) {
        nameLabel.text = name
        iconView.image = UIImage(systemName: symbolName)
        visibleButton.setImage(UIImage(systemName: visible ? "eye.fill" : "eye.slash"), for: .normal)
        lockedButton.setImage(UIImage(systemName: locked ? "lock.fill" : "lock.open"), for: .normal)
        stackLeadingConstraint?.constant = TimelineLayerTree.leadingInset(depth: depth, isProperty: false)
        contentView.alpha = 1
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
}

@MainActor
private final class TimelinePropertyCell: UITableViewCell {
    static let reuseID = "TimelinePropertyCell"

    var onTap: (() -> Void)?

    private let nameLabel = UILabel()
    private let badgeView = UIImageView()
    private let selectionBackground = UIView()
    private var nameLeadingConstraint: NSLayoutConstraint?

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

        let nameLeading = nameLabel.leadingAnchor.constraint(equalTo: contentView.leadingAnchor,
                                                             constant: TimelineLayerTree.propertyLeading)
        nameLeadingConstraint = nameLeading
        NSLayoutConstraint.activate([
            selectionBackground.topAnchor.constraint(equalTo: contentView.topAnchor),
            selectionBackground.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            selectionBackground.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            selectionBackground.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
            nameLeading,
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

    func configure(label: String, hasKeyframeAtPlayhead: Bool, depth: Int) {
        nameLabel.text = label
        setHasKeyframeAtPlayhead(hasKeyframeAtPlayhead)
        nameLeadingConstraint?.constant = TimelineLayerTree.leadingInset(depth: depth, isProperty: true)
        contentView.alpha = 1
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

//
//  EditorRootView.swift
//  MotionStudioApp
//
//  Top-level editor: layer panel / canvas / inspector / timeline, plus the
//  system UndoManager integration for the core command stack.
//

import SwiftUI

struct EditorRootView: View {
    let document: MotionDocument

    @State private var editorState = EditorState()
    @Environment(\.undoManager) private var undoManager
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass

    var body: some View {
        layout
    }

    #if os(macOS)
    private var layout: some View {
        VStack(spacing: 0) {
            HSplitView {
                LayerPanelView(document: document,
                               editorState: editorState,
                               perform: perform)
                    .frame(minWidth: 170, idealWidth: 210)
                CanvasContainer(document: document, editorState: editorState)
                    .frame(minWidth: 320)
                InspectorView(document: document,
                              editorState: editorState,
                              perform: perform)
                    .frame(minWidth: 230, idealWidth: 270)
            }
            Divider()
            TimelineView(document: document,
                         editorState: editorState,
                         perform: perform,
                         registerEdit: registerEdit)
                .frame(minHeight: 150, idealHeight: 210)
        }
    }
    #else
    private var layout: some View {
        NavigationSplitView {
            LayerPanelView(document: document,
                           editorState: editorState,
                           perform: perform)
                .navigationTitle("Layers")
        } detail: {
            VStack(spacing: 0) {
                HStack(spacing: 0) {
                    CanvasContainer(document: document, editorState: editorState)
                    if horizontalSizeClass == .regular {
                        Divider()
                        InspectorView(document: document,
                                      editorState: editorState,
                                      perform: perform)
                            .frame(width: 280)
                    }
                }
                Divider()
                TimelineView(document: document,
                             editorState: editorState,
                             perform: perform,
                             registerEdit: registerEdit)
                    .frame(height: 200)
            }
            .inspector(isPresented: .constant(horizontalSizeClass != .regular)) {
                InspectorView(document: document,
                              editorState: editorState,
                              perform: perform)
            }
        }
    }
    #endif

    // MARK: - Undo integration
    //
    // The core keeps its own undo stack; the system UndoManager mirrors it so
    // ⌘Z / three-finger swipe and the document's "unsaved changes" tracking
    // work. Each edit registers one action; the action's closure performs the
    // core undo/redo and registers the inverse, keeping both stacks in sync.

    /// Runs an edit command and registers it with the system UndoManager.
    private func perform(_ actionName: String, edit: () -> Void) {
        edit()
        registerEdit(actionName)
    }

    /// Registers an already-executed edit with the system UndoManager.
    /// Call on drag end (the core merged the drag into one command).
    private func registerEdit(_ actionName: String) {
        guard let undoManager else { return }
        undoManager.setActionName(actionName)
        registerInverse(redo: false, undoManager: undoManager)
    }

    private func registerInverse(redo: Bool, undoManager: UndoManager) {
        undoManager.registerUndo(withTarget: document.core) { core in
            if redo {
                core.performRedo()
            } else {
                core.performUndo()
            }
            self.registerInverse(redo: !redo, undoManager: undoManager)
        }
    }
}

/// Wires the canvas to the document with a stable composition ID.
private struct CanvasContainer: View {
    let document: MotionDocument
    let editorState: EditorState

    var body: some View {
        let core = document.core
        let compositionID = core.firstCompositionID
        CanvasView(core: core,
                   compositionID: compositionID,
                   playheadFrame: editorState.playheadFrame,
                   isPlaying: editorState.isPlaying,
                   duration: core.duration(compositionID: compositionID),
                   frameRate: core.frameRate(compositionID: compositionID)) { frame in
            editorState.playheadFrame = frame
        }
    }
}

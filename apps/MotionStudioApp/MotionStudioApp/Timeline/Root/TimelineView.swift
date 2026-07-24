//
//  TimelineView.swift
//  MotionStudioApp
//
//  After-Effects/Figma-Motion-style timeline entry point.
//

import SwiftUI

struct TimelineView: View {
    let document: MotionProjectState
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void

    var body: some View {
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())

        TimelineContentView(duration: duration,
                            frameRate: core.frameRate(compositionID: compositionID),
                            rows: buildTimelineRows(core: core, layerIDs: layerIDs),
                            perform: perform,
                            registerEdit: registerEdit,
                            clearSelection: clearSelection)
            .environment(core)
            .environment(editorState)
    }
}

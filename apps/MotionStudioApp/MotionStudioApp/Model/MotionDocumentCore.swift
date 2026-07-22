//
//  MotionDocumentCore.swift
//  MotionStudioApp
//
//  Swift facade over the C++ core via the C ABI bridge.
//

import Foundation
import Observation

/// Snapshot of one keyframe for UI display.
struct KeyframeInfo: Equatable, Identifiable {
    let frame: Int64
    let value: Float
    let easing: EasingInfo

    var id: Int64 {
        frame
    }
}

/// Easing curve descriptor mirroring the bridge MS_EASING_* tags.
struct EasingInfo: Equatable {
    enum Kind: Int32, Equatable {
        case linear = 0
        case bezier = 1
        case hold = 2
    }

    var kind: Kind
    var inX: Float = 0
    var inY: Float = 0
    var outX: Float = 1
    var outY: Float = 1

    static let linear = EasingInfo(kind: .linear)
    static let hold = EasingInfo(kind: .hold)
    static let easeIn = EasingInfo(kind: .bezier, inX: 0.42, inY: 0, outX: 1, outY: 1)
    static let easeOut = EasingInfo(kind: .bezier, inX: 0, inY: 0, outX: 0.58, outY: 1)
}

/// Owns the C++ document handle and exposes queries, undoable edits, and
/// serialization to the SwiftUI layer.
///
/// The document model lives in C++; SwiftUI cannot track its individual
/// properties. `revision` is bumped after every mutation so views that read
/// it re-evaluate — the standard pattern for reference-backed document models.
@MainActor
@Observable
final class MotionDocumentCore {
    /// Bumped after every mutation; views read it to subscribe to changes.
    private(set) var revision: Int = 0

    @ObservationIgnored
    nonisolated(unsafe) private let handle: OpaquePointer

    /// Invoked after every mutation, on the main actor. The document wires this
    /// to its objectWillChange publisher so the system tracks the edited state
    /// (title-bar dot, close prompt, Save menu item). nonisolated(unsafe) so the
    /// nonisolated document init can install it; it is set once before any mutation.
    nonisolated(unsafe) var onDidChange: (@MainActor () -> Void)?

    /// Construction is main-actor independent (plain C calls), so document
    /// creation works from @Sendable contexts like DocumentGroup.makeDocument.
    nonisolated init() {
        guard let handle = ms_document_create() else {
            fatalError("ms_document_create returned null")
        }
        self.handle = handle
    }

    nonisolated init(json: Data) throws {
        var error: UnsafeMutablePointer<CChar>?
        let handle = json.withUnsafeBytes { buffer in
            ms_document_load(buffer.bindMemory(to: CChar.self).baseAddress, buffer.count, &error)
        }
        guard let handle else {
            let message = Self.takeString(error) ?? "unknown load error"
            throw NSError(domain: "MotionStudio", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: message])
        }
        self.handle = handle
    }

    deinit {
        ms_document_destroy(handle)
    }

    // MARK: - Serialization

    /// Serializes off the main actor: ReferenceFileDocument snapshots run in
    /// a background isolation domain. Thread safety is provided by the bridge
    /// (every C API call locks the document mutex).
    nonisolated func serialize() throws -> Data {
        guard let cString = ms_document_save(handle) else {
            throw NSError(domain: "MotionStudio", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "serialization failed"])
        }
        defer { ms_string_free(cString) }
        return Data(bytes: cString, count: strlen(cString))
    }

    // MARK: - Undo / redo

    var canUndo: Bool {
        ms_document_can_undo(handle)
    }

    var canRedo: Bool {
        ms_document_can_redo(handle)
    }

    /// Executes one undo step. Call from the system UndoManager registration.
    func performUndo() {
        if ms_document_undo(handle) {
            changed()
        }
    }

    /// Executes one redo step. Call from the system UndoManager registration.
    func performRedo() {
        if ms_document_redo(handle) {
            changed()
        }
    }

    /// Closes the core's merge window (call on drag end).
    func endDrag() {
        ms_document_end_merge_group(handle)
    }

    // MARK: - Composition queries

    var firstCompositionID: UInt64 {
        ms_document_composition_id_at(handle, 0)
    }

    func compositionIDs() -> [UInt64] {
        let count = ms_document_composition_count(handle)
        return (0 ..< Int(count)).map { ms_document_composition_id_at(handle, Int32($0)) }
    }

    func compositionName(_ compositionID: UInt64) -> String {
        Self.takeString(ms_composition_name(handle, compositionID)) ?? ""
    }

    func duration(compositionID: UInt64) -> Int64 {
        ms_composition_duration(handle, compositionID)
    }

    func size(compositionID: UInt64) -> CGSize {
        CGSize(width: CGFloat(ms_composition_width(handle, compositionID)),
               height: CGFloat(ms_composition_height(handle, compositionID)))
    }

    func frameRate(compositionID: UInt64) -> Double {
        let num = ms_composition_frame_rate_num(handle, compositionID)
        let den = ms_composition_frame_rate_den(handle, compositionID)
        guard den > 0 else { return 30 }
        return Double(num) / Double(den)
    }

    // MARK: - Layer queries

    func layerIDs(compositionID: UInt64) -> [UInt64] {
        let count = ms_composition_layer_count(handle, compositionID)
        return (0 ..< Int(count)).map { ms_layer_id_at(handle, compositionID, Int32($0)) }
    }

    func layerName(_ layerID: UInt64) -> String {
        Self.takeString(ms_layer_name(handle, layerID)) ?? ""
    }

    func layerIsVisible(_ layerID: UInt64) -> Bool {
        ms_layer_visible(handle, layerID)
    }

    func layerIsLocked(_ layerID: UInt64) -> Bool {
        ms_layer_locked(handle, layerID)
    }

    func layerInPoint(_ layerID: UInt64) -> Int64 {
        ms_layer_in_point(handle, layerID)
    }

    func layerOutPoint(_ layerID: UInt64) -> Int64 {
        ms_layer_out_point(handle, layerID)
    }

    /// MS_LAYER_* type tag; -1 when the layer does not exist.
    func layerType(_ layerID: UInt64) -> Int32 {
        ms_layer_type(handle, layerID)
    }

    // MARK: - Property queries

    /// Whether the entity exposes the given property path at all.
    func hasProperty(entityID: UInt64, path: String) -> Bool {
        ms_property_type(handle, entityID, path) >= 0
    }

    func staticFloat(entityID: UInt64, path: String) -> Float {
        ms_property_static_float(handle, entityID, path)
    }

    func staticVec2(entityID: UInt64, path: String) -> CGVector {
        var x: Float = 0
        var y: Float = 0
        ms_property_static_vec2(handle, entityID, path, &x, &y)
        return CGVector(dx: CGFloat(x), dy: CGFloat(y))
    }

    func isAnimated(entityID: UInt64, path: String) -> Bool {
        ms_property_is_animated(handle, entityID, path)
    }

    func keyframes(entityID: UInt64, path: String) -> [KeyframeInfo] {
        let count = ms_property_keyframe_count(handle, entityID, path)
        var result: [KeyframeInfo] = []
        result.reserveCapacity(Int(count))
        for index in 0 ..< Int(count) {
            let i = Int32(index)
            let frame = Int64(ms_property_keyframe_time_at(handle, entityID, path, i))
            let value = ms_property_keyframe_float_at(handle, entityID, path, i)
            var inX: Float = 0
            var inY: Float = 0
            var outX: Float = 0
            var outY: Float = 0
            let type = ms_property_keyframe_easing_at(handle, entityID, path, i, &inX, &inY,
                                                      &outX, &outY)
            let kind = EasingInfo.Kind(rawValue: type) ?? .linear
            result.append(KeyframeInfo(frame: frame, value: value,
                                       easing: EasingInfo(kind: kind, inX: inX, inY: inY,
                                                          outX: outX, outY: outY)))
        }
        return result
    }

    func evaluateFloat(entityID: UInt64, path: String, frame: Int64) -> Float {
        ms_property_evaluate_float(handle, entityID, path, frame)
    }

    func evaluateVec2(entityID: UInt64, path: String, frame: Int64) -> CGVector {
        var x: Float = 0
        var y: Float = 0
        ms_property_evaluate_vec2(handle, entityID, path, frame, &x, &y)
        return CGVector(dx: CGFloat(x), dy: CGFloat(y))
    }

    // MARK: - Undoable edits

    // Every method below runs a command through the core undo manager and
    // bumps `revision`. Callers additionally register with the system
    // UndoManager (see EditorRootView.perform).

    func setStaticFloat(entityID: UInt64, path: String, value: Float) {
        ms_command_set_static_float(handle, entityID, path, value)
        changed()
    }

    func setStaticVec2(entityID: UInt64, path: String, value: CGVector) {
        ms_command_set_static_vec2(handle, entityID, path, Float(value.dx), Float(value.dy))
        changed()
    }

    func addKeyframeFloat(entityID: UInt64, path: String, frame: Int64, value: Float) {
        ms_command_add_keyframe_float(handle, entityID, path, frame, value)
        changed()
    }

    func addKeyframeVec2(entityID: UInt64, path: String, frame: Int64, value: CGVector) {
        ms_command_add_keyframe_vec2(handle, entityID, path, frame,
                                     Float(value.dx), Float(value.dy))
        changed()
    }

    func removeKeyframe(entityID: UInt64, path: String, frame: Int64) {
        ms_command_remove_keyframe(handle, entityID, path, frame)
        changed()
    }

    func moveKeyframe(entityID: UInt64, path: String, from: Int64, to: Int64) {
        ms_command_move_keyframe(handle, entityID, path, from, to)
        changed()
    }

    func setEasing(entityID: UInt64, path: String, frame: Int64, easing: EasingInfo) {
        ms_command_set_easing(handle, entityID, path, frame, easing.kind.rawValue,
                              easing.inX, easing.inY, easing.outX, easing.outY)
        changed()
    }

    @discardableResult
    func addRectLayer(compositionID: UInt64) -> UInt64 {
        let layerID = ms_command_add_rect_layer(handle, compositionID)
        changed()
        return layerID
    }

    @discardableResult
    func addEllipseLayer(compositionID: UInt64) -> UInt64 {
        let layerID = ms_command_add_ellipse_layer(handle, compositionID)
        changed()
        return layerID
    }

    func removeLayer(compositionID: UInt64, layerID: UInt64) {
        ms_command_remove_layer(handle, compositionID, layerID)
        changed()
    }

    func setLayerVisible(_ layerID: UInt64, visible: Bool) {
        ms_command_set_layer_visible(handle, layerID, visible)
        changed()
    }

    func setLayerLocked(_ layerID: UInt64, locked: Bool) {
        ms_command_set_layer_locked(handle, layerID, locked)
        changed()
    }

    // MARK: - Canvas

    /// Draws the composition at frame into the canvas's MTKView drawable.
    func drawFrame(canvas: OpaquePointer, compositionID: UInt64, frame: Int64) {
        ms_canvas_draw_frame(canvas, handle, compositionID, frame)
    }

    private func changed() {
        revision += 1
        onDidChange?()
    }

    private nonisolated static func takeString(_ cString: UnsafeMutablePointer<CChar>?) -> String? {
        guard let cString else { return nil }
        defer { ms_string_free(cString) }
        return String(cString: cString)
    }
}

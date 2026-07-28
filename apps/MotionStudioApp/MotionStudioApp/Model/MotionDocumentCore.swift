//
//  MotionDocumentCore.swift
//  MotionStudioApp
//
//  Swift facade over the C++ core via the C ABI bridge.
//

import CoreGraphics
import Foundation
import MotionStudioBridging
import Observation

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
    private nonisolated(unsafe) let handle: OpaquePointer

    /// Invoked after every mutation, on the main actor. The document wires this
    /// to its objectWillChange publisher so the system tracks the edited state
    /// (title-bar dot, close prompt, Save menu item).
    @ObservationIgnored
    var onDidChange: (@MainActor () -> Void)?

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

    /// Serializes off the main actor for document save snapshots. Thread safety
    /// is provided by the bridge
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

    /// Opens a drag transaction so mixed property edits become one undo unit.
    func beginDrag() {
        ms_document_begin_merge_group(handle)
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

    func backgroundColor(compositionID: UInt64) -> MotionColor {
        var r: Float = 0
        var g: Float = 0
        var b: Float = 0
        var a: Float = 1
        ms_composition_background_color(handle, compositionID, &r, &g, &b, &a)
        return MotionColor(r: r, g: g, b: b, a: a)
    }

    func cornerRadius(compositionID: UInt64) -> Float {
        ms_composition_corner_radius(handle, compositionID)
    }

    func hitTestLayer(compositionID: UInt64, frameTime: Double, point: CGPoint, tolerance: CGFloat) -> UInt64? {
        let layerID = ms_composition_hit_test_layer(handle, compositionID, frameTime,
                                                    Float(point.x), Float(point.y),
                                                    Float(max(tolerance, 0)))
        return layerID == 0 ? nil : layerID
    }

    func selectionHandles(compositionID: UInt64,
                          frameTime: Double,
                          layerIDs: [UInt64],
                          primaryLayerID: UInt64) -> SelectionHandlesSnapshot?
    {
        var handles = MSSelectionHandles()
        let ok = layerIDs.withUnsafeBufferPointer { buffer in
            ms_composition_selection_handles(handle,
                                             compositionID,
                                             frameTime,
                                             buffer.baseAddress,
                                             buffer.count,
                                             primaryLayerID,
                                             &handles)
        }
        guard ok else {
            return nil
        }
        return SelectionHandlesSnapshot(handles)
    }

    func hitTestSelectionHandle(_ snapshot: SelectionHandlesSnapshot,
                                point: CGPoint,
                                handleHitRadius: CGFloat,
                                rotateInner: CGFloat,
                                rotateOuter: CGFloat) -> MS_SELECTION_HANDLE
    {
        var handles = snapshot.bridgeValue
        return ms_selection_handles_hit_test(&handles,
                                             Float(point.x),
                                             Float(point.y),
                                             Float(handleHitRadius),
                                             Float(rotateInner),
                                             Float(rotateOuter))
    }

    func layerBounds(compositionID: UInt64, layerID: UInt64, frameTime: Double) -> CGRect? {
        var minX: Float = 0
        var minY: Float = 0
        var maxX: Float = 0
        var maxY: Float = 0
        guard ms_composition_layer_bounds(handle, compositionID, layerID, frameTime,
                                          &minX, &minY, &maxX, &maxY)
        else {
            return nil
        }
        return CGRect(x: CGFloat(minX),
                      y: CGFloat(minY),
                      width: CGFloat(maxX - minX),
                      height: CGFloat(maxY - minY))
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

    /// MS_LAYER_* type tag; `.INVALID` when the layer does not exist.
    func layerType(_ layerID: UInt64) -> MS_LAYER {
        ms_layer_type(handle, layerID)
    }

    // MARK: - Layer style queries

    /// Number of styles (fills/strokes) on the layer.
    func styleCount(layerID: UInt64) -> Int {
        Int(ms_layer_style_count(handle, layerID))
    }

    /// MS_STYLE_* type tag of the style at index; `.INVALID` when out of range.
    func styleType(layerID: UInt64, index: Int) -> MS_STYLE {
        ms_layer_style_type_at(handle, layerID, Int32(index))
    }

    /// MS_BLEND_* blend mode tag of the style at index; `.INVALID` when out of range.
    func styleBlendMode(layerID: UInt64, index: Int) -> MS_BLEND {
        ms_layer_style_blend_mode_at(handle, layerID, Int32(index))
    }

    /// MS_STROKE_POSITION_* tag of the stroke at index; `.INVALID` when not a stroke.
    func strokePosition(layerID: UInt64, index: Int) -> MS_STROKE_POSITION {
        ms_layer_style_stroke_position_at(handle, layerID, Int32(index))
    }

    // MARK: - Mask / track matte queries

    func maskCount(layerID: UInt64) -> Int {
        Int(ms_layer_mask_count(handle, layerID))
    }

    /// MS_MASK_* tag at index; `.INVALID` when out of range.
    func maskMode(layerID: UInt64, index: Int) -> MS_MASK {
        ms_layer_mask_mode_at(handle, layerID, Int32(index))
    }

    func maskInverted(layerID: UInt64, index: Int) -> Bool {
        ms_layer_mask_inverted_at(handle, layerID, Int32(index))
    }

    /// MS_TRACK_MATTE_* tag; None when the layer has no track matte.
    func trackMatteType(layerID: UInt64) -> MS_TRACK_MATTE {
        ms_layer_track_matte_type(handle, layerID)
    }

    /// Matte source layer id; 0 when none.
    func trackMatteLayerID(layerID: UInt64) -> UInt64 {
        ms_layer_track_matte_layer_id(handle, layerID)
    }

    // MARK: - Property queries

    /// Whether the entity exposes the given property path at all.
    func hasProperty(entityID: UInt64, path: String) -> Bool {
        ms_property_type(handle, entityID, path) != .INVALID
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

    func staticColor(entityID: UInt64, path: String) -> MotionColor {
        var r: Float = 0
        var g: Float = 0
        var b: Float = 0
        var a: Float = 0
        ms_property_static_color(handle, entityID, path, &r, &g, &b, &a)
        return MotionColor(r: r, g: g, b: b, a: a)
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
            let kind = ms_property_keyframe_easing_at(handle, entityID, path, i, &inX, &inY,
                                                      &outX, &outY)
            let resolvedKind: MS_EASING = kind == .INVALID ? .LINEAR : kind
            result.append(KeyframeInfo(frame: frame, value: value,
                                       easing: EasingInfo(kind: resolvedKind, inX: inX, inY: inY,
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

    func evaluateColor(entityID: UInt64, path: String, frame: Int64) -> MotionColor {
        var r: Float = 0
        var g: Float = 0
        var b: Float = 0
        var a: Float = 0
        ms_property_evaluate_color(handle, entityID, path, frame, &r, &g, &b, &a)
        return MotionColor(r: r, g: g, b: b, a: a)
    }

    /// Frames of all keyframes on the property, ascending. Type-agnostic.
    func keyframeFrames(entityID: UInt64, path: String) -> [Int64] {
        let count = ms_property_keyframe_count(handle, entityID, path)
        return (0 ..< Int(count)).map { ms_property_keyframe_time_at(handle, entityID, path, Int32($0)) }
    }

    // MARK: - Undoable edits

    // Every method below runs a command through the core undo manager and
    // bumps `revision`. Callers additionally register with the system
    // UndoManager from the editor controller.

    func setStaticFloat(entityID: UInt64, path: String, value: Float) {
        ms_command_set_static_float(handle, entityID, path, value)
        changed()
    }

    func setStaticVec2(entityID: UInt64, path: String, value: CGVector) {
        ms_command_set_static_vec2(handle, entityID, path, Float(value.dx), Float(value.dy))
        changed()
    }

    func setStaticColor(entityID: UInt64, path: String, value: MotionColor) {
        ms_command_set_static_color(handle, entityID, path, value.r, value.g, value.b, value.a)
        changed()
    }

    func setCompositionBackgroundColor(compositionID: UInt64, value: MotionColor) {
        ms_command_set_composition_background_color(handle, compositionID,
                                                    value.r, value.g, value.b, value.a)
        changed()
    }

    func setCompositionCornerRadius(compositionID: UInt64, value: Float) {
        ms_command_set_composition_corner_radius(handle, compositionID, value)
        changed()
    }

    func setCompositionSize(compositionID: UInt64, size: CGSize) {
        ms_command_set_composition_size(handle, compositionID,
                                        Int32(max(Int(size.width.rounded()), 1)),
                                        Int32(max(Int(size.height.rounded()), 1)))
        changed()
    }

    func setCompositionDuration(compositionID: UInt64, duration: Int64) {
        ms_command_set_composition_duration(handle, compositionID, max(duration, 1))
        changed()
    }

    func setCompositionFrameRate(compositionID: UInt64, framesPerSecond: Float) {
        let frameRate = Self.rationalFrameRate(framesPerSecond)
        ms_command_set_composition_frame_rate(handle, compositionID,
                                              Int32(frameRate.num),
                                              Int32(frameRate.den))
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

    func addKeyframeColor(entityID: UInt64, path: String, frame: Int64, value: MotionColor) {
        ms_command_add_keyframe_color(handle, entityID, path, frame,
                                      value.r, value.g, value.b, value.a)
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

    @discardableResult
    func addPathLayer(compositionID: UInt64) -> UInt64 {
        let layerID = ms_command_add_path_layer(handle, compositionID)
        changed()
        return layerID
    }

    func convertGeometryToPath(layerID: UInt64, frame: Int64) {
        ms_command_convert_geometry_to_path(handle, layerID, frame)
        changed()
    }

    func hasBezierPath(entityID: UInt64, path: String) -> Bool {
        ms_property_type(handle, entityID, path) == .BEZIER_PATH
    }

    func pathEditMoveVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                            index: Int, scenePoint: CGPoint, linkedHandles: Bool)
    {
        ms_command_path_edit_move_vertex(handle, layerID, kind, Int32(maskIndex), frame,
                                         index, Float(scenePoint.x), Float(scenePoint.y),
                                         linkedHandles)
        changed()
    }

    func pathEditMoveInTangent(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                               index: Int, scenePoint: CGPoint, mirrorOut: Bool)
    {
        ms_command_path_edit_move_in_tangent(handle, layerID, kind, Int32(maskIndex), frame,
                                             index, Float(scenePoint.x), Float(scenePoint.y),
                                             mirrorOut)
        changed()
    }

    func pathEditMoveOutTangent(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                                index: Int, scenePoint: CGPoint, mirrorIn: Bool)
    {
        ms_command_path_edit_move_out_tangent(handle, layerID, kind, Int32(maskIndex), frame,
                                              index, Float(scenePoint.x), Float(scenePoint.y),
                                              mirrorIn)
        changed()
    }

    func pathEditInsertOnSegment(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                                 segmentIndex: Int, t: Float)
    {
        ms_command_path_edit_insert_on_segment(handle, layerID, kind, Int32(maskIndex), frame,
                                               segmentIndex, t)
        changed()
    }

    func pathEditRemoveVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                              index: Int)
    {
        ms_command_path_edit_remove_vertex(handle, layerID, kind, Int32(maskIndex), frame, index)
        changed()
    }

    func pathEditClose(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64) {
        ms_command_path_edit_close(handle, layerID, kind, Int32(maskIndex), frame)
        changed()
    }

    func pathEditAppendVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                              scenePoint: CGPoint)
    {
        ms_command_path_edit_append_vertex(handle, layerID, kind, Int32(maskIndex), frame,
                                           Float(scenePoint.x), Float(scenePoint.y))
        changed()
    }

    func pathEditToggleSmooth(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                              index: Int)
    {
        ms_command_path_edit_toggle_smooth(handle, layerID, kind, Int32(maskIndex), frame, index)
        changed()
    }

    func pathEditRecenterShape(layerID: UInt64, frame: Int64) {
        ms_command_path_edit_recenter_shape(handle, layerID, frame)
        changed()
    }

    func removeLayer(compositionID: UInt64, layerID: UInt64) {
        ms_command_remove_layer(handle, compositionID, layerID)
        changed()
    }

    /// Removes multiple layers as one undo unit when `layerIDs.count > 1`.
    func removeLayers(compositionID: UInt64, layerIDs: [UInt64]) {
        guard !layerIDs.isEmpty else {
            return
        }
        if layerIDs.count == 1 {
            removeLayer(compositionID: compositionID, layerID: layerIDs[0])
            return
        }
        ms_document_begin_merge_group(handle)
        for layerID in layerIDs {
            ms_command_remove_layer(handle, compositionID, layerID)
        }
        ms_document_end_merge_group(handle)
        changed()
    }

    func moveLayer(compositionID: UInt64, fromIndex: Int, toIndex: Int) {
        ms_command_move_layer(handle, compositionID, Int32(fromIndex), Int32(toIndex))
        changed()
    }

    /// Applies an absolute model-order (bottom → top). No-op when already equal.
    func applyLayerOrder(compositionID: UInt64, desired: [UInt64]) {
        let current = layerIDs(compositionID: compositionID)
        guard current != desired else {
            return
        }
        for step in TimelineReorder.moveSteps(from: current, to: desired) {
            ms_command_move_layer(handle, compositionID, Int32(step.from), Int32(step.to))
        }
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

    /// Appends a default fill to the layer's style list.
    func addFillStyle(layerID: UInt64) {
        ms_command_add_fill_style(handle, layerID)
        changed()
    }

    /// Removes the style at index from the layer's style list.
    func removeStyle(layerID: UInt64, index: Int) {
        ms_command_remove_style(handle, layerID, Int32(index))
        changed()
    }

    func addStrokeStyle(layerID: UInt64) {
        ms_command_add_stroke_style(handle, layerID)
        changed()
    }

    /// blendMode: MS_BLEND_* tag. Applies to fill and stroke styles.
    func setStyleBlendMode(layerID: UInt64, index: Int, blendMode: MS_BLEND) {
        ms_command_set_style_blend_mode(handle, layerID, Int32(index), blendMode)
        changed()
    }

    /// position: MS_STROKE_POSITION_* tag. Only applies to stroke styles.
    func setStrokePosition(layerID: UInt64, index: Int, position: MS_STROKE_POSITION) {
        ms_command_set_stroke_position(handle, layerID, Int32(index), position)
        changed()
    }

    /// Appends a path mask baked from the layer's shape at `frame` (Add mode).
    func addMask(layerID: UInt64, frame: Int64) {
        ms_command_add_mask(handle, layerID, frame)
        changed()
    }

    func removeMask(layerID: UInt64, index: Int) {
        ms_command_remove_mask(handle, layerID, Int32(index))
        changed()
    }

    func moveMask(layerID: UInt64, fromIndex: Int, toIndex: Int) {
        ms_command_move_mask(handle, layerID, Int32(fromIndex), Int32(toIndex))
        changed()
    }

    /// mode: MS_MASK_* tag.
    func setMaskMode(layerID: UInt64, index: Int, mode: MS_MASK) {
        ms_command_set_mask_mode(handle, layerID, Int32(index), mode)
        changed()
    }

    func setMaskInverted(layerID: UInt64, index: Int, inverted: Bool) {
        ms_command_set_mask_inverted(handle, layerID, Int32(index), inverted)
        changed()
    }

    /// type: MS_TRACK_MATTE_*. matteLayerID may be 0 when type is None.
    func setTrackMatte(layerID: UInt64, matteLayerID: UInt64, type: MS_TRACK_MATTE) {
        ms_command_set_track_matte(handle, layerID, matteLayerID, type)
        changed()
    }

    // MARK: - Canvas

    /// Draws the composition at frame into the canvas's MTKView drawable.
    func drawFrame(canvas: OpaquePointer, compositionID: UInt64, frame: Int64) {
        ms_canvas_draw_frame(canvas, handle, compositionID, frame)
    }

    /// Draws one frame and returns per-stage CPU timing for profiling.
    func drawFrameProfiled(canvas: OpaquePointer, compositionID: UInt64, frame: Int64) -> CanvasFrameProfile {
        var profile = MSCanvasFrameProfile()
        ms_canvas_draw_frame_profiled(canvas, handle, compositionID, frame, &profile)
        return CanvasFrameProfile(profile)
    }

    /// Draws one preview frame at fractional frame time and returns per-stage CPU timing.
    func drawFrameProfiled(canvas: OpaquePointer, compositionID: UInt64, frameTime: Double) -> CanvasFrameProfile {
        var profile = MSCanvasFrameProfile()
        ms_canvas_draw_frame_at_time_profiled(canvas, handle, compositionID, frameTime, &profile)
        return CanvasFrameProfile(profile)
    }

    func hitPathEdit(canvas: OpaquePointer, compositionID: UInt64, frameTime: Double,
                     point: CGPoint) -> MSPathEditHit
    {
        ms_canvas_hit_path_edit(canvas, handle, compositionID, frameTime,
                                Float(point.x), Float(point.y))
    }

    private func changed() {
        revision += 1
        onDidChange?()
    }

    private nonisolated static func rationalFrameRate(_ framesPerSecond: Float) -> (num: UInt32, den: UInt32) {
        let denominator: UInt32 = 1000
        let fps = max(Double(framesPerSecond), 0.001)
        let numerator = UInt32(max(1, min(Double(UInt32.max), (fps * Double(denominator)).rounded())))
        let divisor = greatestCommonDivisor(numerator, denominator)
        return (numerator / divisor, denominator / divisor)
    }

    private nonisolated static func greatestCommonDivisor(_ lhs: UInt32, _ rhs: UInt32) -> UInt32 {
        var a = lhs
        var b = rhs
        while b != 0 {
            let remainder = a % b
            a = b
            b = remainder
        }
        return max(a, 1)
    }

    private nonisolated static func takeString(_ cString: UnsafeMutablePointer<CChar>?) -> String? {
        guard let cString else { return nil }
        defer { ms_string_free(cString) }
        return String(cString: cString)
    }
}

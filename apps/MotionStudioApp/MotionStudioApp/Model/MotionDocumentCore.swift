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

enum VideoExportError: Error {
    case cancelled
    case failed(String)
}

enum PagExportError: Error {
    case failed(String)
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

    convenience nonisolated init(json: Data) throws {
        try self.init(documentJSON: json, shadersJSON: nil)
    }

    /// Loads document.json and shader.json together into one in-memory Document.
    nonisolated init(documentJSON: Data, shadersJSON: Data?) throws {
        var error: UnsafeMutablePointer<CChar>?
        let handle = documentJSON.withUnsafeBytes { documentBuffer -> OpaquePointer? in
            let documentBase = documentBuffer.bindMemory(to: CChar.self).baseAddress
            let documentCount = documentBuffer.count
            guard let shadersJSON else {
                return ms_document_load_json_with_shaders(documentBase, documentCount, nil, 0, &error)
            }
            return shadersJSON.withUnsafeBytes { shadersBuffer in
                ms_document_load_json_with_shaders(
                    documentBase, documentCount,
                    shadersBuffer.bindMemory(to: CChar.self).baseAddress, shadersBuffer.count,
                    &error,
                )
            }
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

    nonisolated func serializeShaders() throws -> Data {
        guard let cString = ms_document_serialize_shaders(handle) else {
            throw NSError(domain: "MotionStudio", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "shader serialization failed"])
        }
        defer { ms_string_free(cString) }
        return Data(bytes: cString, count: strlen(cString))
    }

    // MARK: - Shader library

    func shaderIDs() -> [UInt64] {
        let count = ms_document_shader_count(handle)
        return (0 ..< Int(count)).compactMap { index in
            let id = ms_document_shader_id_at(handle, Int32(index))
            return id == 0 ? nil : id
        }
    }

    func shaderName(_ shaderID: UInt64) -> String {
        Self.takeString(ms_document_shader_name(handle, shaderID)) ?? ""
    }

    func shaderMainImage(_ shaderID: UInt64) -> String {
        Self.takeString(ms_document_shader_main_image(handle, shaderID)) ?? ""
    }

    func shaderUniformCount(_ shaderID: UInt64) -> Int {
        Int(ms_document_shader_uniform_count(handle, shaderID))
    }

    func shaderUniformName(_ shaderID: UInt64, index: Int) -> String {
        Self.takeString(ms_document_shader_uniform_name_at(handle, shaderID, Int32(index))) ?? ""
    }

    func shaderUniformFormat(_ shaderID: UInt64, index: Int) -> MS_UNIFORM_FORMAT {
        ms_document_shader_uniform_format_at(handle, shaderID, Int32(index))
    }

    @discardableResult
    func addShader(name: String) -> UInt64 {
        let id = ms_document_add_shader(handle, name)
        if id != 0 {
            changed()
        }
        return id
    }

    @discardableResult
    func updateShader(id: UInt64, name: String, mainImage: String, uniformsJSON: String) -> Bool {
        let ok = ms_document_update_shader(handle, id, name, mainImage, uniformsJSON)
        if ok {
            changed()
        }
        return ok
    }

    @discardableResult
    func removeShader(_ shaderID: UInt64) -> Bool {
        let ok = ms_document_remove_shader(handle, shaderID)
        if ok {
            changed()
        }
        return ok
    }

    func shaderIsReferenced(_ shaderID: UInt64) -> Bool {
        ms_document_shader_is_referenced(handle, shaderID)
    }

    @discardableResult
    func renameShader(_ shaderID: UInt64, name: String) -> Bool {
        let ok = ms_document_rename_shader(handle, shaderID, name)
        if ok {
            changed()
        }
        return ok
    }

    func stylePaintMode(layerID: UInt64, index: Int) -> MS_PAINT_MODE {
        ms_layer_style_paint_mode_at(handle, layerID, Int32(index))
    }

    func styleShaderID(layerID: UInt64, index: Int) -> UInt64 {
        ms_layer_style_shader_id_at(handle, layerID, Int32(index))
    }

    @discardableResult
    func setStylePaintMode(layerID: UInt64, index: Int, mode: MS_PAINT_MODE, shaderID: UInt64) -> Bool {
        let ok = ms_document_set_style_paint_mode(handle, layerID, Int32(index), mode, shaderID)
        if ok {
            changed()
        }
        return ok
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

    /// Opens a merge window so mixed property edits become one undo unit.
    func beginMergeGroup() {
        ms_document_begin_merge_group(handle)
    }

    /// Closes the core's merge window.
    func endMergeGroup() {
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

    /// Runs on the calling thread. `progress` may be invoked off the main actor.
    nonisolated func exportVideo(compositionID: UInt64,
                                 outputPath: String,
                                 resolved: VideoExportResolvedSettings,
                                 progress: (@Sendable (Int64, Int64) -> Bool)?,
                                 cancelState: VideoExportCancelState) throws
    {
        final class ProgressBox: @unchecked Sendable {
            let progress: (@Sendable (Int64, Int64) -> Bool)?
            init(_ progress: (@Sendable (Int64, Int64) -> Bool)?) {
                self.progress = progress
            }
        }
        let box = ProgressBox(progress)
        try outputPath.withCString { path in
            var options = MSVideoExportOptions()
            options.outputPath = path
            options.startFrame = 0
            options.endFrame = resolved.durationFrames
            options.width = Int32(resolved.width)
            options.height = Int32(resolved.height)
            options.frameRateNum = Int32(resolved.frameRateNum)
            options.frameRateDen = Int32(resolved.frameRateDen)
            options.bitrateBps = Int32(resolved.bitrateBps)
            options.keyframeInterval = 0
            options.profile = Int32(resolved.profile)

            var error: UnsafeMutablePointer<CChar>?
            let ok = ms_video_export(
                handle,
                compositionID,
                &options,
                { ctx, completed, total in
                    guard let ctx else { return true }
                    let box = Unmanaged<ProgressBox>.fromOpaque(ctx).takeUnretainedValue()
                    return box.progress?(completed, total) ?? true
                },
                Unmanaged.passUnretained(box).toOpaque(),
                UnsafeRawPointer(cancelState.flagPointer).assumingMemoryBound(to: Int32.self),
                &error,
            )
            if ok {
                return
            }
            let message = Self.takeString(error) ?? "export failed"
            if message == "cancelled" {
                throw VideoExportError.cancelled
            }
            throw VideoExportError.failed(message)
        }
    }

    nonisolated func exportPAG(compositionID: UInt64,
                               outputPath: String,
                               allowBitmapExport: Bool,
                               bmpSequenceType: MS_PAG_BMP_SEQUENCE_TYPE = .AUTO) throws
    {
        try outputPath.withCString { path in
            var options = MSPagExportOptions()
            options.outputPath = path
            options.allowBitmapExport = allowBitmapExport
            options.bitmapScale = 1.0
            options.bmpSequenceType = bmpSequenceType

            var error: UnsafeMutablePointer<CChar>?
            let ok = ms_pag_export(handle, compositionID, &options, &error)
            if ok {
                return
            }
            let message = Self.takeString(error) ?? "export failed"
            throw PagExportError.failed(message)
        }
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

    func mapCompositionDelta(compositionID: UInt64, layerID: UInt64, frame: Int64,
                             delta: CGVector) -> CGVector?
    {
        var outX: Float = 0
        var outY: Float = 0
        guard ms_layer_map_composition_delta(handle, compositionID, layerID, Double(frame),
                                             Float(delta.dx), Float(delta.dy), &outX, &outY)
        else {
            return nil
        }
        return CGVector(dx: CGFloat(outX), dy: CGFloat(outY))
    }

    /// Aligns layers by translating stored `transform.position`. Caller owns merge group.
    func alignLayers(compositionID: UInt64, layerIDs: [UInt64], edge: LayerAlignEdge, frame: Int64) {
        guard !layerIDs.isEmpty else { return }
        let target: CGRect
        if layerIDs.count == 1 {
            let size = size(compositionID: compositionID)
            target = CGRect(origin: .zero, size: size)
        } else {
            let rects = layerIDs.compactMap {
                layerBounds(compositionID: compositionID, layerID: $0, frameTime: Double(frame))
            }
            guard let union = LayerAlign.unionBounds(rects) else { return }
            target = union
        }
        let path = TransformProperty.position.path
        for layerID in layerIDs {
            guard let bounds = layerBounds(compositionID: compositionID, layerID: layerID,
                                           frameTime: Double(frame))
            else {
                continue
            }
            let deltaComp = LayerAlign.compositionDelta(edge: edge, bounds: bounds, target: target)
            if abs(deltaComp.dx) < 1e-6, abs(deltaComp.dy) < 1e-6 {
                continue
            }
            guard let deltaParent = mapCompositionDelta(compositionID: compositionID,
                                                        layerID: layerID,
                                                        frame: frame,
                                                        delta: deltaComp)
            else {
                continue
            }
            let stored = evaluateVec2(entityID: layerID, path: path, frame: frame)
            let next = CGVector(dx: stored.dx + deltaParent.dx, dy: stored.dy + deltaParent.dy)
            if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
                addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: next)
            } else {
                setStaticVec2(entityID: layerID, path: path, value: next)
            }
        }
    }

    /// Translates selected layers by a composition-space delta. Caller owns merge group.
    func nudgeLayersPosition(compositionID: UInt64,
                             layerIDs: [UInt64],
                             delta: CGVector,
                             frame: Int64)
    {
        guard !layerIDs.isEmpty else { return }
        if abs(delta.dx) < 1e-6, abs(delta.dy) < 1e-6 { return }
        let path = TransformProperty.position.path
        for layerID in layerIDs {
            guard let deltaParent = mapCompositionDelta(compositionID: compositionID,
                                                        layerID: layerID,
                                                        frame: frame,
                                                        delta: delta)
            else {
                continue
            }
            let stored = evaluateVec2(entityID: layerID, path: path, frame: frame)
            let next = CGVector(dx: stored.dx + deltaParent.dx,
                                dy: stored.dy + deltaParent.dy)
            if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
                addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: next)
            } else {
                setStaticVec2(entityID: layerID, path: path, value: next)
            }
        }
    }

    func layerLocalBounds(compositionID: UInt64, layerID: UInt64, frameTime: Double) -> CGRect? {
        var minX: Float = 0
        var minY: Float = 0
        var maxX: Float = 0
        var maxY: Float = 0
        guard ms_layer_local_bounds(handle, compositionID, layerID, frameTime,
                                    &minX, &minY, &maxX, &maxY)
        else {
            return nil
        }
        return CGRect(x: CGFloat(minX),
                      y: CGFloat(minY),
                      width: CGFloat(maxX - minX),
                      height: CGFloat(maxY - minY))
    }

    /// Parent-space offset from local AABB top-left to the anchor.
    func layoutPositionOffset(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector {
        let anchor = evaluateVec2(entityID: layerID,
                                  path: TransformProperty.anchorPoint.path,
                                  frame: frame)
        let scale = evaluateVec2(entityID: layerID,
                                 path: TransformProperty.scale.path,
                                 frame: frame)
        let rotation = evaluateFloat(entityID: layerID,
                                     path: TransformProperty.rotation.path,
                                     frame: frame)
        guard let bounds = layerLocalBounds(compositionID: compositionID,
                                            layerID: layerID,
                                            frameTime: Double(frame))
        else {
            return .zero
        }
        return LayoutPosition.offset(anchor: anchor,
                                     scale: scale,
                                     rotationDegrees: rotation,
                                     localBounds: bounds)
    }

    func evaluateLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector {
        let stored = evaluateVec2(entityID: layerID,
                                  path: TransformProperty.position.path,
                                  frame: frame)
        let offset = layoutPositionOffset(compositionID: compositionID,
                                          layerID: layerID,
                                          frame: frame)
        return LayoutPosition.toLayout(stored: stored, offset: offset)
    }

    /// Writes layout (top-left) position; converts to stored AE position.
    /// Upserts keyframe when playhead already has one otherwise setStatic.
    func writeLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64,
                             value: CGVector)
    {
        let offset = layoutPositionOffset(compositionID: compositionID,
                                          layerID: layerID,
                                          frame: frame)
        let stored = LayoutPosition.toStored(layout: value, offset: offset)
        let path = TransformProperty.position.path
        if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
            addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: stored)
        } else {
            setStaticVec2(entityID: layerID, path: path, value: stored)
        }
    }

    func keyframeLayoutPosition(compositionID: UInt64, layerID: UInt64, index: Int) -> CGVector {
        let path = TransformProperty.position.path
        let frames = keyframeFrames(entityID: layerID, path: path)
        guard index >= 0, index < frames.count else { return .zero }
        let stored = keyframeVec2(entityID: layerID, path: path, index: index)
        let offset = layoutPositionOffset(compositionID: compositionID,
                                          layerID: layerID,
                                          frame: frames[index])
        return LayoutPosition.toLayout(stored: stored, offset: offset)
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

    // MARK: - Follow Path queries

    func followPathEnabled(layerID: UInt64) -> Bool {
        ms_layer_follow_path_enabled(handle, layerID)
    }

    /// Path source layer id; 0 when unbound.
    func followPathLayerID(layerID: UInt64) -> UInt64 {
        ms_layer_follow_path_layer_id(handle, layerID)
    }

    func followPathOrient(layerID: UInt64) -> Bool {
        ms_layer_follow_path_orient(handle, layerID)
    }

    // MARK: - Text Path queries

    func textPathEnabled(layerID: UInt64) -> Bool {
        ms_layer_text_path_enabled(handle, layerID)
    }

    /// Path source layer id; 0 when unbound.
    func textPathLayerID(layerID: UInt64) -> UInt64 {
        ms_layer_text_path_layer_id(handle, layerID)
    }

    func textPathReversed(layerID: UInt64) -> Bool {
        ms_layer_text_path_reversed(handle, layerID)
    }

    func textPathPerpendicular(layerID: UInt64) -> Bool {
        ms_layer_text_path_perpendicular(handle, layerID)
    }

    func textPathForceAlignment(layerID: UInt64) -> Bool {
        ms_layer_text_path_force_alignment(handle, layerID)
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

    func staticVec3(entityID: UInt64, path: String) -> SIMD3<Float> {
        var x: Float = 0
        var y: Float = 0
        var z: Float = 0
        ms_property_static_vec3(handle, entityID, path, &x, &y, &z)
        return SIMD3(x, y, z)
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

    func evaluateVec3(entityID: UInt64, path: String, frame: Int64) -> SIMD3<Float> {
        var x: Float = 0
        var y: Float = 0
        var z: Float = 0
        ms_property_evaluate_vec3(handle, entityID, path, frame, &x, &y, &z)
        return SIMD3(x, y, z)
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

    func keyframeVec2(entityID: UInt64, path: String, index: Int) -> CGVector {
        var x: Float = 0
        var y: Float = 0
        ms_property_keyframe_vec2_at(handle, entityID, path, Int32(index), &x, &y)
        return CGVector(dx: CGFloat(x), dy: CGFloat(y))
    }

    func keyframeSpatial(entityID: UInt64, path: String, index: Int) -> SpatialTangentsInfo {
        var hasIn = false
        var hasOut = false
        var inX: Float = 0
        var inY: Float = 0
        var outX: Float = 0
        var outY: Float = 0
        guard ms_property_keyframe_spatial_at(handle, entityID, path, Int32(index), &hasIn, &inX, &inY,
                                              &hasOut, &outX, &outY)
        else {
            return SpatialTangentsInfo()
        }
        return SpatialTangentsInfo(hasIn: hasIn, inTangent: CGVector(dx: CGFloat(inX), dy: CGFloat(inY)),
                                   hasOut: hasOut,
                                   outTangent: CGVector(dx: CGFloat(outX), dy: CGFloat(outY)))
    }

    func setSpatialTangents(entityID: UInt64, path: String, frame: Int64,
                            hasIn: Bool, inTangent: CGVector,
                            hasOut: Bool, outTangent: CGVector)
    {
        ms_command_set_spatial_tangents(handle, entityID, path, frame, hasIn,
                                        Float(inTangent.dx), Float(inTangent.dy), hasOut,
                                        Float(outTangent.dx), Float(outTangent.dy))
        changed()
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

    func setStaticVec3(entityID: UInt64, path: String, value: SIMD3<Float>) {
        ms_command_set_static_vec3(handle, entityID, path, value.x, value.y, value.z)
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

    func addKeyframeVec3(entityID: UInt64, path: String, frame: Int64, value: SIMD3<Float>) {
        ms_command_add_keyframe_vec3(handle, entityID, path, frame, value.x, value.y, value.z)
        changed()
    }

    func addKeyframeColor(entityID: UInt64, path: String, frame: Int64, value: MotionColor) {
        ms_command_add_keyframe_color(handle, entityID, path, frame,
                                      value.r, value.g, value.b, value.a)
        changed()
    }

    /// Adds a path keyframe at `frame` using the evaluated VectorNetwork.
    /// Must not round-trip through BezierPath — that keeps only the first fill
    /// face and destroys shared-vertex networks.
    func addKeyframeBezierPathAtPlayhead(entityID: UInt64, path: String, frame: Int64) {
        guard let evaluated = ms_property_evaluate_vector_network(handle, entityID, path, frame) else {
            return
        }
        defer { ms_vector_network_free(evaluated) }
        ms_command_add_keyframe_vector_network(handle, entityID, path, frame, evaluated)
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

    // MARK: - Image assets / layers

    nonisolated func setProjectRoot(_ absolutePath: String?) {
        if let absolutePath {
            absolutePath.withCString { ms_document_set_project_root(handle, $0) }
        } else {
            ms_document_set_project_root(handle, nil)
        }
    }

    nonisolated func projectRoot() -> String? {
        Self.takeString(ms_document_project_root(handle))
    }

    /// Copies the file into `{projectRoot}/assets/` and registers a document asset.
    @discardableResult
    func importImageAsset(sourceURL: URL, preferredFileName: String?, width: Int, height: Int) -> UInt64 {
        let assetID = sourceURL.path(percentEncoded: false).withCString { sourcePath in
            if let preferredFileName {
                return preferredFileName.withCString { name in
                    ms_command_import_image_asset(handle, sourcePath, name, Int32(width), Int32(height))
                }
            }
            return ms_command_import_image_asset(handle, sourcePath, nil, Int32(width), Int32(height))
        }
        if assetID != 0 {
            changed()
        }
        return assetID
    }

    @discardableResult
    func addImageLayer(compositionID: UInt64) -> UInt64 {
        let layerID = ms_command_add_image_layer(handle, compositionID)
        changed()
        return layerID
    }

    @discardableResult
    func setImageAsset(layerID: UInt64, assetID: UInt64, frame: Int64? = nil) -> Bool {
        let previousAssetID = imageAssetID(layerID: layerID)
        let firstBind = previousAssetID == 0 && assetID != 0
        if firstBind {
            beginMergeGroup()
        }
        let ok = ms_layer_set_image_asset(handle, layerID, assetID)
        if ok {
            changed()
            if firstBind, let frame {
                applyResetImageSizeToIntrinsic(layerID: layerID, frame: frame)
            }
        }
        if firstBind {
            endMergeGroup()
        }
        return ok
    }

    func imageAssetID(layerID: UInt64) -> UInt64 {
        ms_layer_image_asset_id(handle, layerID)
    }

    func imageScaleMode(layerID: UInt64) -> MS_IMAGE_SCALE {
        ms_layer_image_scale_mode(handle, layerID)
    }

    func setImageScaleMode(layerID: UInt64, mode: MS_IMAGE_SCALE) {
        ms_layer_set_image_scale_mode(handle, layerID, mode)
        changed()
    }

    @discardableResult
    func addTextLayer(compositionID: UInt64) -> UInt64 {
        let layerID = ms_command_add_text_layer(handle, compositionID)
        changed()
        return layerID
    }

    func setTextFont(layerID: UInt64, family: String, style: String) {
        family.withCString { cFamily in
            style.withCString { cStyle in
                _ = ms_command_set_text_font(handle, layerID, cFamily, cStyle)
            }
        }
        changed()
    }

    func setTextBoxTextMode(layerID: UInt64, boxTextMode: Bool, frame: Int64 = 0) {
        _ = ms_command_set_text_box_text_mode(handle, layerID, boxTextMode, frame)
        changed()
    }

    func textFontSize(layerID: UInt64) -> Float {
        ms_layer_text_font_size(handle, layerID)
    }

    func setTextFontSize(layerID: UInt64, fontSize: Float) {
        _ = ms_command_set_text_font_size(handle, layerID, fontSize)
        changed()
    }

    func textSize(layerID: UInt64) -> CGVector {
        var width: Float = 0
        var height: Float = 0
        guard ms_layer_text_size(handle, layerID, &width, &height) else {
            return .zero
        }
        return CGVector(dx: CGFloat(width), dy: CGFloat(height))
    }

    func setTextSize(layerID: UInt64, size: CGVector) {
        _ = ms_command_set_text_size(handle, layerID, Float(size.dx), Float(size.dy))
        changed()
    }

    func setTextAlign(layerID: UInt64, align: MS_TEXT_ALIGN) {
        _ = ms_command_set_text_align(handle, layerID, align)
        changed()
    }

    func textBoxTextMode(layerID: UInt64) -> Bool {
        ms_layer_text_box_text_mode(handle, layerID)
    }

    func textAlign(layerID: UInt64) -> MS_TEXT_ALIGN {
        ms_layer_text_align(handle, layerID)
    }

    func textFontFamily(layerID: UInt64) -> String {
        Self.takeString(ms_layer_text_font_family(handle, layerID)) ?? ""
    }

    func textFontStyle(layerID: UInt64) -> String {
        Self.takeString(ms_layer_text_font_style(handle, layerID)) ?? ""
    }

    func staticString(entityID: UInt64, path: String) -> String {
        Self.takeString(ms_property_static_string(handle, entityID, path)) ?? ""
    }

    func setStaticString(entityID: UInt64, path: String, value: String) {
        value.withCString { cValue in
            ms_command_set_static_string(handle, entityID, path, cValue)
        }
        changed()
    }

    /// Updates text box size and scales the anchor proportionally; position stays fixed.
    func setTextBoxSize(layerID: UInt64, size: CGVector, frame: Int64) {
        let oldSize = textSize(layerID: layerID)
        let oldAnchor = evaluateVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame)
        let ratioX = oldSize.dx > 1e-6 ? size.dx / oldSize.dx : 1
        let ratioY = oldSize.dy > 1e-6 ? size.dy / oldSize.dy : 1
        let newAnchor = CGVector(dx: oldAnchor.dx * ratioX, dy: oldAnchor.dy * ratioY)
        beginMergeGroup()
        setTextSize(layerID: layerID, size: size)
        writeVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame, value: newAnchor)
        endMergeGroup()
    }

    func layerBlendMode(layerID: UInt64) -> MS_BLEND {
        ms_layer_blend_mode(handle, layerID)
    }

    func setLayerBlendMode(layerID: UInt64, blendMode: MS_BLEND) {
        ms_command_set_layer_blend_mode(handle, layerID, blendMode)
        changed()
    }

    /// Sets `image.size` to the bound asset's intrinsic pixel size, centers the
    /// anchor on the new size, and compensates `position` so the container center
    /// stays fixed in scene space.
    func resetImageSizeToIntrinsic(layerID: UInt64, frame: Int64) {
        beginMergeGroup()
        applyResetImageSizeToIntrinsic(layerID: layerID, frame: frame)
        endMergeGroup()
    }

    /// Caller must own the merge group when combining with other edits.
    private func applyResetImageSizeToIntrinsic(layerID: UInt64, frame: Int64) {
        let assetID = imageAssetID(layerID: layerID)
        guard assetID != 0 else {
            return
        }
        let width = Float(ms_asset_width(handle, assetID))
        let height = Float(ms_asset_height(handle, assetID))
        guard width > 0, height > 0 else {
            return
        }

        let oldSize = evaluateVec2(entityID: layerID, path: ImageProperty.size.path, frame: frame)
        let oldAnchor = evaluateVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame)
        let oldPosition = evaluateVec2(entityID: layerID, path: TransformProperty.position.path, frame: frame)
        let rotation = evaluateFloat(entityID: layerID, path: TransformProperty.rotation.path, frame: frame)
        let scale = evaluateVec2(entityID: layerID, path: TransformProperty.scale.path, frame: frame)

        let oldCenterLocal = CGPoint(x: oldSize.dx * 0.5, y: oldSize.dy * 0.5)
        let localRelative = CGPoint(x: oldCenterLocal.x - oldAnchor.dx, y: oldCenterLocal.y - oldAnchor.dy)
        let scaled = CGPoint(x: localRelative.x * scale.dx, y: localRelative.y * scale.dy)
        let radians = CGFloat(rotation) * .pi / 180
        let cosine = cos(radians)
        let sine = sin(radians)
        let rotated = CGPoint(x: scaled.x * cosine - scaled.y * sine,
                              y: scaled.x * sine + scaled.y * cosine)
        let centerScene = CGVector(dx: oldPosition.dx + rotated.x, dy: oldPosition.dy + rotated.y)

        let newSize = CGVector(dx: CGFloat(width), dy: CGFloat(height))
        let newAnchor = CGVector(dx: CGFloat(width) * 0.5, dy: CGFloat(height) * 0.5)

        writeVec2(entityID: layerID, path: ImageProperty.size.path, frame: frame, value: newSize)
        writeVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame, value: newAnchor)
        writeVec2(entityID: layerID, path: TransformProperty.position.path, frame: frame, value: centerScene)
    }

    private func writeVec2(entityID: UInt64, path: String, frame: Int64, value: CGVector) {
        if isAnimated(entityID: entityID, path: path) {
            addKeyframeVec2(entityID: entityID, path: path, frame: frame, value: value)
        } else {
            setStaticVec2(entityID: entityID, path: path, value: value)
        }
    }

    func assetIDs() -> [UInt64] {
        let count = ms_document_asset_count(handle)
        return (0 ..< Int(count)).compactMap { index in
            let id = ms_document_asset_id_at(handle, Int32(index))
            return id == 0 ? nil : id
        }
    }

    func imageAssetIDs() -> [UInt64] {
        assetIDs().filter { ms_asset_type(handle, $0) == 0 }
    }

    func assetName(_ assetID: UInt64) -> String {
        Self.takeString(ms_asset_name(handle, assetID)) ?? ""
    }

    func assetPath(_ assetID: UInt64) -> String {
        Self.takeString(ms_asset_path(handle, assetID)) ?? ""
    }

    func assetWidth(_ assetID: UInt64) -> Int {
        Int(ms_asset_width(handle, assetID))
    }

    func assetHeight(_ assetID: UInt64) -> Int {
        Int(ms_asset_height(handle, assetID))
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

    func pathEditSetMirrorMode(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                               vertexId: UInt32, mode: MS_VERTEX_MIRROR)
    {
        ms_command_path_edit_set_mirror_mode(handle, layerID, kind, Int32(maskIndex), frame, vertexId,
                                             mode)
        changed()
    }

    func pathEditRecenterShape(layerID: UInt64, frame: Int64) {
        ms_command_path_edit_recenter_shape(handle, layerID, frame)
        changed()
    }

    @discardableResult
    func networkEditAddVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                              scenePoint: CGPoint) -> UInt32
    {
        var vertexId: UInt32 = 0
        ms_command_network_edit_add_vertex(handle, layerID, kind, Int32(maskIndex), frame,
                                           Float(scenePoint.x), Float(scenePoint.y), &vertexId)
        changed()
        return vertexId
    }

    @discardableResult
    func networkEditAddEdge(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                            startId: UInt32, endId: UInt32) -> UInt32
    {
        var edgeId: UInt32 = 0
        ms_command_network_edit_add_edge(handle, layerID, kind, Int32(maskIndex), frame, startId,
                                         endId, &edgeId)
        changed()
        return edgeId
    }

    func networkEditMoveVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                               vertexId: UInt32, scenePoint: CGPoint)
    {
        ms_command_network_edit_move_vertex(handle, layerID, kind, Int32(maskIndex), frame, vertexId,
                                            Float(scenePoint.x), Float(scenePoint.y))
        changed()
    }

    func networkEditMoveEdgeTangent(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int,
                                    frame: Int64, edgeId: UInt32, atStart: Bool,
                                    scenePoint: CGPoint, mirror: Bool)
    {
        ms_command_network_edit_move_edge_tangent(handle, layerID, kind, Int32(maskIndex), frame,
                                                  edgeId, atStart, Float(scenePoint.x),
                                                  Float(scenePoint.y), mirror)
        changed()
    }

    @discardableResult
    func networkEditInsertOnEdge(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                                 edgeId: UInt32, t: Float) -> UInt32
    {
        var vertexId: UInt32 = 0
        ms_command_network_edit_insert_on_edge(handle, layerID, kind, Int32(maskIndex), frame,
                                               edgeId, t, &vertexId)
        changed()
        return vertexId
    }

    func networkEditRemoveVertex(layerID: UInt64, kind: MS_PATH_EDIT, maskIndex: Int, frame: Int64,
                                 vertexId: UInt32)
    {
        ms_command_network_edit_remove_vertex(handle, layerID, kind, Int32(maskIndex), frame,
                                              vertexId)
        changed()
    }

    func networkEditRecenterShape(layerID: UInt64, frame: Int64) {
        ms_command_network_edit_recenter_shape(handle, layerID, frame)
        changed()
    }

    /// Index of `vertexId` in the evaluated network, or -1 when missing.
    func networkVertexIndex(entityID: UInt64, path: String, frame: Int64, vertexId: UInt32) -> Int {
        guard vertexId != 0,
              let network = ms_property_evaluate_vector_network(handle, entityID, path, frame)
        else {
            return -1
        }
        defer { ms_vector_network_free(network) }
        let count = network.pointee.vertexCount
        guard count > 0, let vertices = network.pointee.vertices else {
            return -1
        }
        for index in 0 ..< count {
            if vertices[index].id == vertexId {
                return Int(index)
            }
        }
        return -1
    }

    func networkVertexMirrorMode(entityID: UInt64, path: String, frame: Int64,
                                 vertexId: UInt32) -> MS_VERTEX_MIRROR
    {
        guard vertexId != 0,
              let network = ms_property_evaluate_vector_network(handle, entityID, path, frame)
        else {
            return .NONE
        }
        defer { ms_vector_network_free(network) }
        let count = network.pointee.vertexCount
        guard count > 0, let vertices = network.pointee.vertices else {
            return .NONE
        }
        for index in 0 ..< count {
            if vertices[index].id == vertexId {
                return vertices[index].mirrorMode
            }
        }
        return .NONE
    }

    func networkVertexDegree(entityID: UInt64, path: String, frame: Int64, vertexId: UInt32) -> Int {
        guard vertexId != 0,
              let network = ms_property_evaluate_vector_network(handle, entityID, path, frame)
        else {
            return 0
        }
        defer { ms_vector_network_free(network) }
        let edgeCount = network.pointee.edgeCount
        guard edgeCount > 0, let edges = network.pointee.edges else {
            return 0
        }
        var degree = 0
        for index in 0 ..< edgeCount {
            if edges[index].start == vertexId || edges[index].end == vertexId {
                degree += 1
            }
        }
        return degree
    }

    /// Scales shape geometry and mask paths in layer-local space about `localPivot`.
    /// One-shot relative to *current* geometry — FreeTransform must apply from a drag-start snapshot.
    @discardableResult
    func resizeLayerGeometry(layerID: UInt64, frame: Int64, localPivot: CGPoint, scaleX: CGFloat,
                             scaleY: CGFloat) -> Bool
    {
        let ok = ms_command_resize_layer_geometry(handle, layerID, Double(frame), Float(localPivot.x),
                                                  Float(localPivot.y), Float(scaleX), Float(scaleY))
        if ok {
            changed()
        }
        return ok
    }

    /// Evaluated BezierPath copy at `frame`, or nil when missing / empty.
    func evaluateBezierPath(entityID: UInt64, path: String, frame: Int64) -> CapturedBezierPath? {
        guard let evaluated = ms_property_evaluate_bezier_path(handle, entityID, path, frame) else {
            return nil
        }
        defer { ms_bezier_path_free(evaluated) }
        return CapturedBezierPath(msPath: evaluated)
    }

    /// Evaluated VectorNetwork at `frame`, or nil when missing / empty.
    func evaluateVectorNetwork(entityID: UInt64, path: String, frame: Int64) -> CapturedVectorNetwork? {
        guard let evaluated = ms_property_evaluate_vector_network(handle, entityID, path, frame) else {
            return nil
        }
        defer { ms_vector_network_free(evaluated) }
        let captured = CapturedVectorNetwork(msNetwork: evaluated)
        return captured.vertices.isEmpty ? nil : captured
    }

    func writeBezierPathAtPlayhead(entityID: UInt64, path: String, frame: Int64,
                                   value: CapturedBezierPath)
    {
        value.withMSBezierPath { msPath in
            ms_command_write_bezier_path_at_playhead(handle, entityID, path, frame, msPath)
        }
        changed()
    }

    func writeVectorNetworkAtPlayhead(entityID: UInt64, path: String, frame: Int64,
                                      value: CapturedVectorNetwork)
    {
        value.withMSVectorNetwork { msNetwork in
            ms_command_write_vector_network_at_playhead(handle, entityID, path, frame, msNetwork)
        }
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

    func setFollowPath(layerID: UInt64, enabled: Bool, pathLayerID: UInt64, orientAlongPath: Bool) {
        ms_command_set_follow_path(handle, layerID, enabled, pathLayerID, orientAlongPath)
        changed()
    }

    func setTextPath(layerID: UInt64, enabled: Bool, pathLayerID: UInt64, reversed: Bool,
                     perpendicular: Bool, forceAlignment: Bool)
    {
        ms_command_set_text_path(handle, layerID, enabled, pathLayerID, reversed, perpendicular,
                                 forceAlignment)
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

    func hitMotionPath(canvas: OpaquePointer, compositionID: UInt64, frameTime: Double,
                       point: CGPoint) -> MSMotionPathHit
    {
        ms_canvas_hit_motion_path(canvas, handle, compositionID, frameTime,
                                  Float(point.x), Float(point.y))
    }

    func setMotionPathSelection(canvas: OpaquePointer, layerID: UInt64?, selectedKeyframe: Int?) {
        ms_canvas_set_motion_path_selection(canvas, layerID ?? 0, Int32(selectedKeyframe ?? -1))
    }

    func dragMotionPathTangent(layerID: UInt64, keyframeIndex: Int, isOut: Bool,
                               scenePoint: CGPoint, frameTime: Double)
    {
        ms_command_motion_path_drag_tangent(handle, layerID, Int32(keyframeIndex), isOut,
                                            Float(scenePoint.x), Float(scenePoint.y), frameTime)
        changed()
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

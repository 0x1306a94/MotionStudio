//
//  FreeTransformDrag.swift
//  MotionStudioApp
//
//  AE-style free-transform drag math (move / scale / rotate / anchor).
//

import CoreGraphics
import Foundation

struct LayerTransformStart {
    let layerID: UInt64
    let position: CGVector
    let scale: CGVector
    let rotation: Float
    let anchor: CGVector
    let contentSize: CGVector
    /// `image.size` or `content.size` when the layer has a box container.
    let contentSizePath: String?
    let positionAnimated: Bool
    let scaleAnimated: Bool
    let rotationAnimated: Bool
    let anchorAnimated: Bool
    let contentSizeAnimated: Bool

    var hasContentSize: Bool {
        contentSizePath != nil
    }
}

enum FreeTransformKind {
    case move
    case scaleCorner(Int)
    case scaleEdge(Int)
    case rotate
    case anchor
}

struct FreeTransformDrag {
    let kind: FreeTransformKind
    let layerStarts: [LayerTransformStart]
    let startScenePoint: CGPoint
    let startHandles: SelectionHandlesSnapshot
    let pivotScene: CGPoint
    /// For oriented single-layer scale: localPivot - startAnchor.
    let localPivotRelative: CGPoint?
    let editName: String

    static func makeLayerStarts(core: MotionDocumentCore, layerIDs: [UInt64], frame: Int64) -> [LayerTransformStart] {
        layerIDs.map { layerID in
            let contentSizePath: String? = if core.hasProperty(entityID: layerID, path: ImageProperty.size.path) {
                ImageProperty.size.path
            } else if core.hasProperty(entityID: layerID, path: TextProperty.size.path) {
                TextProperty.size.path
            } else {
                nil
            }
            return LayerTransformStart(
                layerID: layerID,
                position: core.evaluateVec2(entityID: layerID, path: TransformProperty.position.path, frame: frame),
                scale: core.evaluateVec2(entityID: layerID, path: TransformProperty.scale.path, frame: frame),
                rotation: core.evaluateFloat(entityID: layerID, path: TransformProperty.rotation.path, frame: frame),
                anchor: core.evaluateVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame),
                contentSize: contentSizePath.map {
                    core.evaluateVec2(entityID: layerID, path: $0, frame: frame)
                } ?? .zero,
                contentSizePath: contentSizePath,
                positionAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.position.path),
                scaleAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.scale.path),
                rotationAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.rotation.path),
                anchorAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.anchorPoint.path),
                contentSizeAnimated: contentSizePath.map {
                    core.isAnimated(entityID: layerID, path: $0)
                } ?? false,
            )
        }
    }

    static func pivot(for kind: FreeTransformKind,
                      handles: SelectionHandlesSnapshot,
                      alternate: Bool) -> CGPoint
    {
        if alternate, case .scaleCorner = kind {
            return handles.center
        }
        if alternate, case .scaleEdge = kind {
            return handles.center
        }
        switch kind {
        case .move, .anchor:
            return .zero
        case .rotate:
            if handles.isOriented {
                return handles.anchor
            }
            return handles.center
        case let .scaleCorner(index):
            return handles.corners[(index + 2) % 4]
        case let .scaleEdge(index):
            return handles.edgeMids[(index + 2) % 4]
        }
    }

    static func localPivotRelative(for kind: FreeTransformKind,
                                   handles: SelectionHandlesSnapshot,
                                   start: LayerTransformStart,
                                   alternate: Bool) -> CGPoint?
    {
        guard handles.isOriented else {
            return nil
        }
        if alternate {
            let localCenter = CGPoint(x: (handles.localMin.x + handles.localMax.x) * 0.5,
                                      y: (handles.localMin.y + handles.localMax.y) * 0.5)
            return CGPoint(x: localCenter.x - start.anchor.dx, y: localCenter.y - start.anchor.dy)
        }
        let localCorners = [
            CGPoint(x: handles.localMin.x, y: handles.localMin.y),
            CGPoint(x: handles.localMax.x, y: handles.localMin.y),
            CGPoint(x: handles.localMax.x, y: handles.localMax.y),
            CGPoint(x: handles.localMin.x, y: handles.localMax.y),
        ]
        switch kind {
        case let .scaleCorner(index):
            let pivot = localCorners[(index + 2) % 4]
            return CGPoint(x: pivot.x - start.anchor.dx, y: pivot.y - start.anchor.dy)
        case let .scaleEdge(index):
            let oppositeA = localCorners[(index + 2) % 4]
            let oppositeB = localCorners[(index + 3) % 4]
            let oppositeMid = CGPoint(x: (oppositeA.x + oppositeB.x) * 0.5,
                                      y: (oppositeA.y + oppositeB.y) * 0.5)
            return CGPoint(x: oppositeMid.x - start.anchor.dx, y: oppositeMid.y - start.anchor.dy)
        default:
            return nil
        }
    }

    func apply(core: MotionDocumentCore,
               frame: Int64,
               scenePoint: CGPoint,
               shift: Bool,
               alternate: Bool)
    {
        switch kind {
        case .move:
            applyMove(core: core, frame: frame, scenePoint: scenePoint)
        case .scaleCorner, .scaleEdge:
            applyScale(core: core, frame: frame, scenePoint: scenePoint, shift: shift, alternate: alternate)
        case .rotate:
            applyRotate(core: core, frame: frame, scenePoint: scenePoint, shift: shift)
        case .anchor:
            applyAnchor(core: core, frame: frame, scenePoint: scenePoint)
        }
    }

    private func applyMove(core: MotionDocumentCore, frame: Int64, scenePoint: CGPoint) {
        let delta = CGPoint(x: scenePoint.x - startScenePoint.x, y: scenePoint.y - startScenePoint.y)
        for start in layerStarts {
            let position = CGVector(dx: start.position.dx + delta.x, dy: start.position.dy + delta.y)
            writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                      frame: frame, value: position, animated: start.positionAnimated)
        }
    }

    private func applyScale(core: MotionDocumentCore,
                            frame: Int64,
                            scenePoint: CGPoint,
                            shift: Bool,
                            alternate: Bool)
    {
        let axisX = normalized(startHandles.corners[1] - startHandles.corners[0])
        let axisY = normalized(startHandles.corners[3] - startHandles.corners[0])
        let startOffset = startScenePoint - pivotScene
        let currentOffset = scenePoint - pivotScene
        var scaleX = projectionRatio(current: currentOffset, start: startOffset, axis: axisX)
        var scaleY = projectionRatio(current: currentOffset, start: startOffset, axis: axisY)

        switch kind {
        case .scaleEdge(0), .scaleEdge(2):
            scaleX = 1
        case .scaleEdge(1), .scaleEdge(3):
            scaleY = 1
        default:
            break
        }

        if shift {
            let uniform = (abs(scaleX) + abs(scaleY)) * 0.5
            let signX: CGFloat = scaleX < 0 ? -1 : 1
            let signY: CGFloat = scaleY < 0 ? -1 : 1
            if case .scaleEdge = kind {
                if scaleX == 1 {
                    scaleY = uniform * signY
                } else {
                    scaleX = uniform * signX
                }
            } else {
                scaleX = uniform * signX
                scaleY = uniform * signY
            }
        }

        scaleX = clampedScale(scaleX)
        scaleY = clampedScale(scaleY)

        // Never write transform.scale — resize boxes / move positions only.
        let orientedSingle = startHandles.isOriented && layerStarts.count == 1
        for start in layerStarts {
            if let contentSizePath = start.contentSizePath {
                if orientedSingle {
                    applyContainerResize(core: core,
                                         frame: frame,
                                         start: start,
                                         contentSizePath: contentSizePath,
                                         scaleX: scaleX,
                                         scaleY: scaleY,
                                         alternate: alternate)
                } else {
                    applyBoxResizeAboutPivot(core: core,
                                             frame: frame,
                                             start: start,
                                             contentSizePath: contentSizePath,
                                             scaleX: scaleX,
                                             scaleY: scaleY)
                }
            } else if core.layerType(start.layerID) == .SHAPE {
                applyShapeGeometryResize(core: core, frame: frame, start: start,
                                         scaleX: scaleX, scaleY: scaleY)
            } else {
                let position = CGVector(dx: pivotScene.x + (start.position.dx - pivotScene.x) * scaleX,
                                        dy: pivotScene.y + (start.position.dy - pivotScene.y) * scaleY)
                writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                          frame: frame, value: position, animated: start.positionAnimated)
            }
        }
    }

    private func applyShapeGeometryResize(core: MotionDocumentCore,
                                          frame: Int64,
                                          start: LayerTransformStart,
                                          scaleX: CGFloat,
                                          scaleY: CGFloat)
    {
        let localPivot: CGPoint
        if let relative = localPivotRelative, startHandles.isOriented, layerStarts.count == 1 {
            localPivot = CGPoint(x: relative.x + start.anchor.dx, y: relative.y + start.anchor.dy)
        } else {
            let unscaled = inverseRS(CGPoint(x: pivotScene.x - start.position.dx,
                                             y: pivotScene.y - start.position.dy),
                                     rotationDegrees: start.rotation,
                                     scale: start.scale)
            localPivot = CGPoint(x: unscaled.x + start.anchor.dx, y: unscaled.y + start.anchor.dy)
        }
        _ = core.resizeLayerGeometry(layerID: start.layerID, frame: frame, localPivot: localPivot,
                                     scaleX: scaleX, scaleY: scaleY)
    }

    /// Multi-select / AABB box resize: scale size about the shared scene pivot without rewriting localMin.
    private func applyBoxResizeAboutPivot(core: MotionDocumentCore,
                                          frame: Int64,
                                          start: LayerTransformStart,
                                          contentSizePath: String,
                                          scaleX: CGFloat,
                                          scaleY: CGFloat)
    {
        let newSize = CGVector(dx: max(1, start.contentSize.dx * abs(scaleX)),
                               dy: max(1, start.contentSize.dy * abs(scaleY)))
        let relX = start.contentSize.dx > 1e-6 ? start.anchor.dx / start.contentSize.dx : 0.5
        let relY = start.contentSize.dy > 1e-6 ? start.anchor.dy / start.contentSize.dy : 0.5
        let newAnchor = CGVector(dx: relX * newSize.dx, dy: relY * newSize.dy)
        let position = CGVector(dx: pivotScene.x + (start.position.dx - pivotScene.x) * scaleX,
                                dy: pivotScene.y + (start.position.dy - pivotScene.y) * scaleY)
        writeVec2(core: core, layerID: start.layerID, path: contentSizePath,
                  frame: frame, value: newSize, animated: start.contentSizeAnimated)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.anchorPoint.path,
                  frame: frame, value: newAnchor, animated: start.anchorAnimated)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                  frame: frame, value: position, animated: start.positionAnimated)
    }

    private func applyContainerResize(core: MotionDocumentCore,
                                      frame: Int64,
                                      start: LayerTransformStart,
                                      contentSizePath: String,
                                      scaleX: CGFloat,
                                      scaleY: CGFloat,
                                      alternate: Bool)
    {
        let startMin = startHandles.localMin
        let startSize = start.contentSize
        let resized = Self.resizedLocalBox(for: kind,
                                           startMin: startMin,
                                           startSize: startSize,
                                           scaleX: scaleX,
                                           scaleY: scaleY,
                                           alternate: alternate)
        let newSize = resized.size
        let floatingMin = resized.min

        // Keep anchor at the same relative point inside the container (content origin stays startMin).
        let relX = startSize.dx > 1e-6 ? (start.anchor.dx - startMin.x) / startSize.dx : 0.5
        let relY = startSize.dy > 1e-6 ? (start.anchor.dy - startMin.y) / startSize.dy : 0.5
        let newAnchor = CGVector(dx: startMin.x + relX * newSize.dx,
                                 dy: startMin.y + relY * newSize.dy)

        writeVec2(core: core, layerID: start.layerID, path: contentSizePath,
                  frame: frame, value: newSize, animated: start.contentSizeAnimated)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.anchorPoint.path,
                  frame: frame, value: newAnchor, animated: start.anchorAnimated)

        // Map the fixed floating-space pivot into content space (origin = startMin).
        let startPivotLocal = Self.containerPivotLocal(for: kind,
                                                       localMin: startMin,
                                                       size: startSize,
                                                       alternate: alternate)
        let fixedInContent = CGPoint(x: startMin.x + (startPivotLocal.x - floatingMin.x),
                                     y: startMin.y + (startPivotLocal.y - floatingMin.y))
        let localRel = CGPoint(x: fixedInContent.x - newAnchor.dx, y: fixedInContent.y - newAnchor.dy)
        let position = compensatedPosition(pivot: pivotScene,
                                           rotationDegrees: start.rotation,
                                           scale: start.scale,
                                           localRelative: localRel)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                  frame: frame, value: position, animated: start.positionAnimated)
    }

    /// Resizes the local box with signed scales so dragging past the opposite edge flips the box.
    static func resizedLocalBox(for kind: FreeTransformKind,
                                startMin: CGPoint,
                                startSize: CGVector,
                                scaleX: CGFloat,
                                scaleY: CGFloat,
                                alternate: Bool) -> (min: CGPoint, size: CGVector)
    {
        let startMax = CGPoint(x: startMin.x + startSize.dx, y: startMin.y + startSize.dy)
        if alternate {
            let center = CGPoint(x: (startMin.x + startMax.x) * 0.5,
                                 y: (startMin.y + startMax.y) * 0.5)
            let a = CGPoint(x: center.x + (startMin.x - center.x) * scaleX,
                            y: center.y + (startMin.y - center.y) * scaleY)
            let b = CGPoint(x: center.x + (startMax.x - center.x) * scaleX,
                            y: center.y + (startMax.y - center.y) * scaleY)
            var minX = min(a.x, b.x)
            var maxX = max(a.x, b.x)
            var minY = min(a.y, b.y)
            var maxY = max(a.y, b.y)
            if maxX - minX < 1 {
                minX = center.x - 0.5
                maxX = center.x + 0.5
            }
            if maxY - minY < 1 {
                minY = center.y - 0.5
                maxY = center.y + 0.5
            }
            return (CGPoint(x: minX, y: minY), CGVector(dx: maxX - minX, dy: maxY - minY))
        }

        switch kind {
        case let .scaleCorner(index):
            let corners = [
                CGPoint(x: startMin.x, y: startMin.y),
                CGPoint(x: startMax.x, y: startMin.y),
                CGPoint(x: startMax.x, y: startMax.y),
                CGPoint(x: startMin.x, y: startMax.y),
            ]
            let fixed = corners[(index + 2) % 4]
            let moving = corners[index]
            let newMoving = CGPoint(x: fixed.x + (moving.x - fixed.x) * scaleX,
                                    y: fixed.y + (moving.y - fixed.y) * scaleY)
            return Self.orderedBox(a: fixed, b: newMoving, scaleX: scaleX, scaleY: scaleY)

        case let .scaleEdge(index):
            var newMin = startMin
            var newMax = startMax
            switch index {
            case 0: // top — fixed bottom
                let axis = Self.clampedAxis(fixed: startMax.y,
                                            moving: startMax.y + (startMin.y - startMax.y) * scaleY,
                                            scale: scaleY)
                newMin.y = axis.min
                newMax.y = axis.max
            case 1: // right — fixed left
                let axis = Self.clampedAxis(fixed: startMin.x,
                                            moving: startMin.x + (startMax.x - startMin.x) * scaleX,
                                            scale: scaleX)
                newMin.x = axis.min
                newMax.x = axis.max
            case 2: // bottom — fixed top
                let axis = Self.clampedAxis(fixed: startMin.y,
                                            moving: startMin.y + (startMax.y - startMin.y) * scaleY,
                                            scale: scaleY)
                newMin.y = axis.min
                newMax.y = axis.max
            case 3: // left — fixed right
                let axis = Self.clampedAxis(fixed: startMax.x,
                                            moving: startMax.x + (startMin.x - startMax.x) * scaleX,
                                            scale: scaleX)
                newMin.x = axis.min
                newMax.x = axis.max
            default:
                break
            }
            return (newMin, CGVector(dx: newMax.x - newMin.x, dy: newMax.y - newMin.y))

        default:
            return (startMin, startSize)
        }
    }

    private static func orderedBox(a: CGPoint, b: CGPoint, scaleX: CGFloat, scaleY: CGFloat)
        -> (min: CGPoint, size: CGVector)
    {
        let x = clampedAxis(fixed: a.x, moving: b.x, scale: scaleX)
        let y = clampedAxis(fixed: a.y, moving: b.y, scale: scaleY)
        return (CGPoint(x: x.min, y: y.min), CGVector(dx: x.max - x.min, dy: y.max - y.min))
    }

    /// Ensures a minimum extent of 1 while preserving which side of `fixed` the moving edge is on.
    private static func clampedAxis(fixed: CGFloat, moving: CGFloat, scale: CGFloat)
        -> (min: CGFloat, max: CGFloat)
    {
        let delta = moving - fixed
        let signed: CGFloat = if abs(delta) < 1 {
            if delta < 0 || (delta == 0 && scale < 0) {
                -1
            } else {
                1
            }
        } else {
            delta
        }
        let resolved = fixed + signed
        return (min(fixed, resolved), max(fixed, resolved))
    }

    static func containerPivotLocal(for kind: FreeTransformKind,
                                    localMin: CGPoint,
                                    size: CGVector,
                                    alternate: Bool) -> CGPoint
    {
        let localMax = CGPoint(x: localMin.x + size.dx, y: localMin.y + size.dy)
        if alternate {
            return CGPoint(x: (localMin.x + localMax.x) * 0.5, y: (localMin.y + localMax.y) * 0.5)
        }
        let localCorners = [
            CGPoint(x: localMin.x, y: localMin.y),
            CGPoint(x: localMax.x, y: localMin.y),
            CGPoint(x: localMax.x, y: localMax.y),
            CGPoint(x: localMin.x, y: localMax.y),
        ]
        switch kind {
        case let .scaleCorner(index):
            return localCorners[(index + 2) % 4]
        case let .scaleEdge(index):
            let oppositeA = localCorners[(index + 2) % 4]
            let oppositeB = localCorners[(index + 3) % 4]
            return CGPoint(x: (oppositeA.x + oppositeB.x) * 0.5,
                           y: (oppositeA.y + oppositeB.y) * 0.5)
        default:
            return .zero
        }
    }

    private func applyRotate(core: MotionDocumentCore, frame: Int64, scenePoint: CGPoint, shift: Bool) {
        let startAngle = atan2(startScenePoint.y - pivotScene.y, startScenePoint.x - pivotScene.x)
        let currentAngle = atan2(scenePoint.y - pivotScene.y, scenePoint.x - pivotScene.x)
        var deltaDegrees = CGFloat((currentAngle - startAngle) * 180 / .pi)
        if shift {
            deltaDegrees = (deltaDegrees / 45).rounded() * 45
        }
        for start in layerStarts {
            let rotation = start.rotation + Float(deltaDegrees)
            writeFloat(core: core, layerID: start.layerID, path: TransformProperty.rotation.path,
                       frame: frame, value: rotation, animated: start.rotationAnimated)
            if !startHandles.isOriented {
                let relative = CGPoint(x: start.position.dx - pivotScene.x, y: start.position.dy - pivotScene.y)
                let rotated = rotate(relative, degrees: deltaDegrees)
                let position = CGVector(dx: pivotScene.x + rotated.x, dy: pivotScene.y + rotated.y)
                writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                          frame: frame, value: position, animated: start.positionAnimated)
            }
        }
    }

    private func applyAnchor(core: MotionDocumentCore, frame: Int64, scenePoint: CGPoint) {
        guard let start = layerStarts.first else {
            return
        }
        let delta = CGPoint(x: scenePoint.x - startScenePoint.x, y: scenePoint.y - startScenePoint.y)
        let localDelta = inverseRS(delta, rotationDegrees: start.rotation, scale: start.scale)
        let anchor = CGVector(dx: start.anchor.dx + localDelta.x, dy: start.anchor.dy + localDelta.y)
        let position = CGVector(dx: start.position.dx + delta.x, dy: start.position.dy + delta.y)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.anchorPoint.path,
                  frame: frame, value: anchor, animated: start.anchorAnimated)
        writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                  frame: frame, value: position, animated: start.positionAnimated)
    }

    private func writeVec2(core: MotionDocumentCore,
                           layerID: UInt64,
                           path: String,
                           frame: Int64,
                           value: CGVector,
                           animated: Bool)
    {
        if animated {
            core.addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: value)
        } else {
            core.setStaticVec2(entityID: layerID, path: path, value: value)
        }
    }

    private func writeFloat(core: MotionDocumentCore,
                            layerID: UInt64,
                            path: String,
                            frame: Int64,
                            value: Float,
                            animated: Bool)
    {
        if animated {
            core.addKeyframeFloat(entityID: layerID, path: path, frame: frame, value: value)
        } else {
            core.setStaticFloat(entityID: layerID, path: path, value: value)
        }
    }
}

private func projectionRatio(current: CGPoint, start: CGPoint, axis: CGPoint) -> CGFloat {
    let startProjection = start.x * axis.x + start.y * axis.y
    if abs(startProjection) < 1e-4 {
        return 1
    }
    let currentProjection = current.x * axis.x + current.y * axis.y
    return currentProjection / startProjection
}

private func clampedScale(_ value: CGFloat) -> CGFloat {
    if value > 0 {
        return max(value, 1e-3)
    }
    if value < 0 {
        return min(value, -1e-3)
    }
    return 1e-3
}

private func compensatedPosition(pivot: CGPoint,
                                 rotationDegrees: Float,
                                 scale: CGVector,
                                 localRelative: CGPoint) -> CGVector
{
    let scaled = CGPoint(x: localRelative.x * scale.dx, y: localRelative.y * scale.dy)
    let rotated = rotate(scaled, degrees: CGFloat(rotationDegrees))
    return CGVector(dx: pivot.x - rotated.x, dy: pivot.y - rotated.y)
}

private func inverseRS(_ delta: CGPoint, rotationDegrees: Float, scale: CGVector) -> CGPoint {
    let unrotated = rotate(delta, degrees: CGFloat(-rotationDegrees))
    let sx = abs(scale.dx) < 1e-6 ? 1e-6 : scale.dx
    let sy = abs(scale.dy) < 1e-6 ? 1e-6 : scale.dy
    return CGPoint(x: unrotated.x / sx, y: unrotated.y / sy)
}

private func rotate(_ point: CGPoint, degrees: CGFloat) -> CGPoint {
    let radians = degrees * .pi / 180
    let cosine = cos(radians)
    let sine = sin(radians)
    return CGPoint(x: point.x * cosine - point.y * sine, y: point.x * sine + point.y * cosine)
}

private func normalized(_ vector: CGPoint) -> CGPoint {
    let length = hypot(vector.x, vector.y)
    if length < 1e-6 {
        return CGPoint(x: 1, y: 0)
    }
    return CGPoint(x: vector.x / length, y: vector.y / length)
}

private func - (lhs: CGPoint, rhs: CGPoint) -> CGPoint {
    CGPoint(x: lhs.x - rhs.x, y: lhs.y - rhs.y)
}

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
    let positionAnimated: Bool
    let scaleAnimated: Bool
    let rotationAnimated: Bool
    let anchorAnimated: Bool
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
            LayerTransformStart(
                layerID: layerID,
                position: core.evaluateVec2(entityID: layerID, path: TransformProperty.position.path, frame: frame),
                scale: core.evaluateVec2(entityID: layerID, path: TransformProperty.scale.path, frame: frame),
                rotation: core.evaluateFloat(entityID: layerID, path: TransformProperty.rotation.path, frame: frame),
                anchor: core.evaluateVec2(entityID: layerID, path: TransformProperty.anchorPoint.path, frame: frame),
                positionAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.position.path),
                scaleAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.scale.path),
                rotationAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.rotation.path),
                anchorAnimated: core.isAnimated(entityID: layerID, path: TransformProperty.anchorPoint.path),
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
                            alternate _: Bool)
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

        if startHandles.isOriented, let localRel = localPivotRelative, let start = layerStarts.first {
            let newScale = CGVector(dx: start.scale.dx * scaleX, dy: start.scale.dy * scaleY)
            let position = compensatedPosition(pivot: pivotScene,
                                               rotationDegrees: start.rotation,
                                               scale: newScale,
                                               localRelative: localRel)
            writeVec2(core: core, layerID: start.layerID, path: TransformProperty.scale.path,
                      frame: frame, value: newScale, animated: start.scaleAnimated)
            writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                      frame: frame, value: position, animated: start.positionAnimated)
            return
        }

        for start in layerStarts {
            let newScale = CGVector(dx: start.scale.dx * scaleX, dy: start.scale.dy * scaleY)
            let position = CGVector(dx: pivotScene.x + (start.position.dx - pivotScene.x) * scaleX,
                                    dy: pivotScene.y + (start.position.dy - pivotScene.y) * scaleY)
            writeVec2(core: core, layerID: start.layerID, path: TransformProperty.scale.path,
                      frame: frame, value: newScale, animated: start.scaleAnimated)
            writeVec2(core: core, layerID: start.layerID, path: TransformProperty.position.path,
                      frame: frame, value: position, animated: start.positionAnimated)
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

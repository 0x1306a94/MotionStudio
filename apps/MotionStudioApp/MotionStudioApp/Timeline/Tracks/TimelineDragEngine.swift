//
//  TimelineDragEngine.swift
//  MotionStudioApp
//

import Foundation

struct KeyframeMove: Equatable {
    let path: String
    let from: Int64
    let to: Int64
}

enum TimelineDragScope: Equatable {
    case layerScale
    case propertyEdge(path: String)
    case keyframe(path: String, frame: Int64)
}

struct TimelineDragSession: Equatable {
    let scope: TimelineDragScope
    let edge: TimeRangeDragEdge?
    let originStart: Int64
    let originEnd: Int64
    let originFrames: [(path: String, frame: Int64)]

    static func == (lhs: TimelineDragSession, rhs: TimelineDragSession) -> Bool {
        guard lhs.scope == rhs.scope,
              lhs.edge == rhs.edge,
              lhs.originStart == rhs.originStart,
              lhs.originEnd == rhs.originEnd,
              lhs.originFrames.count == rhs.originFrames.count
        else {
            return false
        }
        return zip(lhs.originFrames, rhs.originFrames).allSatisfy { $0.path == $1.path && $0.frame == $1.frame }
    }
}

enum TimelineDragEngine {
    static func makeLayerScaleSession(edge: TimeRangeDragEdge,
                                      originStart: Int64,
                                      originEnd: Int64,
                                      originFrames: [(path: String, frame: Int64)]) -> TimelineDragSession?
    {
        guard originStart < originEnd, !originFrames.isEmpty else {
            return nil
        }
        return TimelineDragSession(scope: .layerScale,
                                   edge: edge,
                                   originStart: originStart,
                                   originEnd: originEnd,
                                   originFrames: originFrames)
    }

    static func makePropertyEdgeSession(path: String,
                                        edge: TimeRangeDragEdge,
                                        originStart: Int64,
                                        originEnd: Int64) -> TimelineDragSession?
    {
        guard originStart < originEnd, !path.isEmpty else {
            return nil
        }
        return TimelineDragSession(scope: .propertyEdge(path: path),
                                   edge: edge,
                                   originStart: originStart,
                                   originEnd: originEnd,
                                   originFrames: [])
    }

    static func makeKeyframeSession(path: String, frame: Int64) -> TimelineDragSession {
        TimelineDragSession(scope: .keyframe(path: path, frame: frame),
                            edge: nil,
                            originStart: frame,
                            originEnd: frame,
                            originFrames: [(path, frame)])
    }

    static func resolve(session: TimelineDragSession,
                        pointerFrame: Int64,
                        duration: Int64,
                        neighbors: (prev: Int64?, next: Int64?)?) -> [KeyframeMove]
    {
        switch session.scope {
        case .layerScale:
            resolveLayerScale(session: session, pointerFrame: pointerFrame, duration: duration)
        case let .propertyEdge(path):
            resolvePropertyEdge(path: path,
                                session: session,
                                pointerFrame: pointerFrame,
                                duration: duration)
        case let .keyframe(path, frame):
            resolveKeyframe(path: path,
                            frame: frame,
                            pointerFrame: pointerFrame,
                            duration: duration,
                            neighbors: neighbors)
        }
    }

    private static func resolveLayerScale(session: TimelineDragSession,
                                          pointerFrame: Int64,
                                          duration: Int64) -> [KeyframeMove]
    {
        guard let edge = session.edge else {
            return []
        }
        let lastFrame = timelineLastInclusiveFrame(duration)
        let anchor: Int64
        let oldEdge: Int64
        let newEdge: Int64
        switch edge {
        case .trailing:
            anchor = session.originStart
            oldEdge = session.originEnd
            newEdge = min(max(pointerFrame, session.originStart + 1), lastFrame)
        case .leading:
            anchor = session.originEnd
            oldEdge = session.originStart
            newEdge = min(max(pointerFrame, 0), session.originEnd - 1)
        }
        guard oldEdge != anchor, newEdge != oldEdge else {
            return []
        }
        let scale = Double(newEdge - anchor) / Double(oldEdge - anchor)
        var moves: [KeyframeMove] = []
        for entry in session.originFrames {
            let mapped = Int64((Double(anchor) + Double(entry.frame - anchor) * scale).rounded())
            let clamped = min(max(mapped, 0), lastFrame)
            if clamped != entry.frame {
                moves.append(KeyframeMove(path: entry.path, from: entry.frame, to: clamped))
            }
        }
        return dedupeMoves(moves)
    }

    private static func resolvePropertyEdge(path: String,
                                            session: TimelineDragSession,
                                            pointerFrame: Int64,
                                            duration: Int64) -> [KeyframeMove]
    {
        guard let edge = session.edge else {
            return []
        }
        let lastFrame = timelineLastInclusiveFrame(duration)
        let from: Int64
        let to: Int64
        switch edge {
        case .leading:
            from = session.originStart
            to = min(max(pointerFrame, 0), session.originEnd - 1)
        case .trailing:
            from = session.originEnd
            to = min(max(pointerFrame, session.originStart + 1), lastFrame)
        }
        guard from != to else {
            return []
        }
        return [KeyframeMove(path: path, from: from, to: to)]
    }

    private static func resolveKeyframe(path: String,
                                        frame: Int64,
                                        pointerFrame: Int64,
                                        duration: Int64,
                                        neighbors: (prev: Int64?, next: Int64?)?) -> [KeyframeMove]
    {
        var lower: Int64 = 0
        var upper: Int64 = timelineLastInclusiveFrame(duration)
        if let prev = neighbors?.prev {
            lower = max(lower, prev + 1)
        }
        if let next = neighbors?.next {
            upper = min(upper, next - 1)
        }
        guard lower <= upper else {
            return []
        }
        let to = min(max(pointerFrame, lower), upper)
        guard to != frame else {
            return []
        }
        return [KeyframeMove(path: path, from: frame, to: to)]
    }

    /// Drop no-ops and keep first mapping per (path, from).
    private static func dedupeMoves(_ moves: [KeyframeMove]) -> [KeyframeMove] {
        var seen = Set<String>()
        var result: [KeyframeMove] = []
        for move in moves where move.from != move.to {
            let key = "\(move.path)#\(move.from)"
            if seen.insert(key).inserted {
                result.append(move)
            }
        }
        return result
    }
}

#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"

namespace motion {

class AnimatableBase;
class Document;

// Property path that locates an Animatable<T> within an entity.
// Syntax (dot-separated segments, array segments written as name[index]):
//   Layer:         "transform.position" / "content.text"
//                  "size" (ShapeContent; resolves against the primary geometry element)
//                  "elements[0].color" (ShapeContent; groups nest via "elements[i]")
//   ShapeElement:  "color" / "path" / "width" ... (entityId points directly at the element)
struct PropertyPath {
    EntityId entityId;  // id of the owning Layer or ShapeElement
    std::string path;

    bool operator==(const PropertyPath &other) const;
    bool operator!=(const PropertyPath &other) const;
};

// Intermediate representation of a parsed property path segment.
struct PathSegment {
    std::string name;
    int index = -1;  // -1 = no array subscript

    bool operator==(const PathSegment &other) const;
};

// Parses "a.b[2].c" into [{a,-1},{b,2},{c,-1}]. Returns empty on invalid format.
// path: the dot-separated property path string.
std::vector<PathSegment> ParsePropertyPath(const std::string &path);

// Resolves a property path to its Animatable. Returns nullptr when the entity
// does not exist, the path is invalid, or the type does not match.
// document: the document to search.
// property: the property path to resolve.
AnimatableBase *ResolveAnimatable(Document &document, const PropertyPath &property);

}  // namespace motion

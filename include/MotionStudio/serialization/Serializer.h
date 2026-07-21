#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "MotionStudio/common/Expected.h"

namespace motion {

class Document;

// Model ↔ JSON v1 (schema documented in docs/data-model.md §6, camelCase
// fields). Deserialization failures (malformed JSON, unknown enum,
// unsupported version) are reported via Expected, never by throwing.
class Serializer {
  public:
    // Serializes a document to indented JSON suitable for writing to a file.
    static std::string serialize(const Document &document);
    // Deserializes a document from a JSON string.
    // jsonText: the full JSON text to parse.
    static Expected<std::unique_ptr<Document>, std::string> deserialize(const std::string &jsonText);
};

// FNV-1a hash of the serialized document. Intended for debug/test assertions
// (e.g. verifying round-trip consistency before and after undo).
uint64_t DocumentFingerprint(const Document &document);

}  // namespace motion

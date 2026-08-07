#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/ShaderDefinition.h"

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

    // Serializes Document.shaders to shader.json text (independent schemaVersion).
    // document: document whose shader library is written.
    static std::string serializeShaders(const Document &document);
    // Deserializes a shader.json string into shader definitions.
    // jsonText: full shader.json text.
    static Expected<std::vector<ShaderDefinition>, std::string> deserializeShaders(
        const std::string &jsonText);
    // Parses a JSON array of {name,format,count} (shader.json uniforms shape).
    // jsonArrayText: JSON array text; empty string or "[]" yields an empty vector.
    static Expected<std::vector<ShaderUniformDecl>, std::string> deserializeUniformDecls(
        const std::string &jsonArrayText);
};

// Checks Shader-mode style references against document.shaders (name/kind alignment).
// document: document with shaders already attached (e.g. after merging deserializeShaders).
Expected<void, std::string> ValidateShaderReferences(const Document &document);

// FNV-1a hash of the serialized document. Intended for debug/test assertions
// (e.g. verifying round-trip consistency before and after undo).
uint64_t DocumentFingerprint(const Document &document);

}  // namespace motion

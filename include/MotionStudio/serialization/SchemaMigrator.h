#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/serialization/ParseError.h"

namespace motion {

// Schema migration chain: v1 → v2 → ... → current. Each step is a pure
// JSON → JSON function. M1 ships only v1; the skeleton is in place so new
// versions can register their migration steps here.
// Inputs and outputs are plain JSON text; the public header exposes no
// third-party JSON types.
class SchemaMigrator {
  public:
    static constexpr int currentVersion() {
        return 1;
    }

    // Reads the embedded schemaVersion and migrates to the current version.
    // Returns an error string on malformed JSON or unsupported version.
    // jsonText: the JSON text to migrate.
    static Expected<std::string, ParseError> migrate(const std::string &jsonText);
};

}  // namespace motion

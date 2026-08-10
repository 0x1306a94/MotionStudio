#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/PagExportOptions.h"

namespace motion {

class BitmapFrameSource;
class Document;

struct PagExportWarning {
    // Related layer or composition; invalid means global.
    EntityId entityId;
    // Stable machine-readable code (see PAG export design §4).
    std::string code;
    // Human-readable detail.
    std::string message;
};

struct PagExportResult {
    std::vector<uint8_t> bytes;
    std::vector<PagExportWarning> warnings;
};

enum class PagExportErrorKind {
    InvalidComposition,
    InvalidOptions,
    MappingFailed,
    EncodeFailed,
    WriteFailed,
};

// Structured export failure. MappingFailed should include entityName + reason.
struct PagExportError {
    PagExportErrorKind kind = PagExportErrorKind::MappingFailed;
    EntityId entityId;
    std::string entityName;
    std::string code;
    std::string message;
};

// Maps a MotionStudio Document composition to a binary .pag file via pag_codec
// (pag::Codec::Encode). The Document must not be mutated during Export.
class PagExporter {
  public:
    // document: source document (immutable for the call duration).
    // options: composition selection, bitmap export flags, optional output path.
    // frameSource: required when allowBitmapExport is true and a _bmp unit needs
    // rasterization; may be nullptr when bitmap export is not triggered.
    static Expected<PagExportResult, PagExportError> Export(
        const Document &document, const PagExportOptions &options,
        BitmapFrameSource *frameSource = nullptr);
};

}  // namespace motion

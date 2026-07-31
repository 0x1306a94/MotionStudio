#include "MotionStudio/export/PagExporter.h"

#include <fstream>

#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/model/Document.h"
#include "PagFileBuilder.h"

namespace motion {
namespace {

const Composition *ResolveComposition(const Document &document, EntityId compositionId) {
    if (compositionId.isValid()) {
        for (const auto &composition : document.compositions) {
            if (composition && composition->id == compositionId) {
                return composition.get();
            }
        }
        return nullptr;
    }
    if (document.compositions.empty() || document.compositions.front() == nullptr) {
        return nullptr;
    }
    return document.compositions.front().get();
}

Expected<void, PagExportError> WriteBytes(const std::string &path,
                                          const std::vector<uint8_t> &bytes) {
    if (path.empty()) {
        return Expected<void, PagExportError>();
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return Unexpected(PagExportError::WriteFailed);
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return Unexpected(PagExportError::WriteFailed);
    }
    return Expected<void, PagExportError>();
}

}  // namespace

Expected<PagExportResult, PagExportError> PagExporter::Export(const Document &document,
                                                              const PagExportOptions &options,
                                                              BitmapFrameSource *frameSource) {
    if (options.bitmapScale <= 0.0f) {
        return Unexpected(PagExportError::InvalidOptions);
    }

    const Composition *composition = ResolveComposition(document, options.compositionId);
    if (composition == nullptr) {
        return Unexpected(PagExportError::InvalidComposition);
    }

    pag_export::PagFileBuilder builder(document, *composition, options, frameSource);
    Expected<pag_export::PagBuildResult, PagExportError> built = builder.build();
    if (!built.hasValue()) {
        return Unexpected(built.error());
    }

    std::unique_ptr<pag::ByteData> encoded = pag::Codec::Encode(built.value().file);
    if (encoded == nullptr || encoded->length() == 0) {
        return Unexpected(PagExportError::EncodeFailed);
    }

    PagExportResult result;
    result.warnings = std::move(built.value().warnings);
    result.bytes.assign(encoded->data(), encoded->data() + encoded->length());

    Expected<void, PagExportError> written = WriteBytes(options.outputPath, result.bytes);
    if (!written.hasValue()) {
        return Unexpected(written.error());
    }
    return result;
}

}  // namespace motion

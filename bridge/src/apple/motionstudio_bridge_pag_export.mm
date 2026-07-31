#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Document.h"
#include "common/DocumentLock.h"

namespace {

void SetError(char **errorOut, const std::string &message) {
    if (errorOut == nullptr) {
        return;
    }
    *errorOut = strdup(message.c_str());
}

const char *MessageForError(motion::PagExportError error, bool allowBitmapFallback) {
    switch (error) {
        case motion::PagExportError::InvalidComposition:
            return "composition not found";
        case motion::PagExportError::InvalidOptions:
            return "invalid PAG export options";
        case motion::PagExportError::MappingFailed:
            if (allowBitmapFallback) {
                return "Bitmap fallback is not available yet";
            }
            return "PAG export mapping failed";
        case motion::PagExportError::EncodeFailed:
            return "PAG encode failed";
        case motion::PagExportError::WriteFailed:
            return "failed to write PAG file";
    }
    return "PAG export failed";
}

}  // namespace

bool ms_pag_export(MSDocument *document, uint64_t compositionId, const MSPagExportOptions *options,
                   char **errorOut) {
    if (errorOut != nullptr) {
        *errorOut = nullptr;
    }
    if (document == nullptr) {
        SetError(errorOut, "document is null");
        return false;
    }
    if (options == nullptr || options->outputPath == nullptr || options->outputPath[0] == '\0') {
        SetError(errorOut, "output path is empty");
        return false;
    }

    DocumentLock lock(document);
    if (document->document == nullptr) {
        SetError(errorOut, "document is null");
        return false;
    }

    motion::PagExportOptions exportOptions;
    exportOptions.outputPath = options->outputPath;
    exportOptions.compositionId = motion::EntityId{compositionId};
    exportOptions.allowBitmapFallback = options->allowBitmapFallback;
    exportOptions.bitmapScale = options->bitmapScale > 0.0f ? options->bitmapScale : 1.0f;

    const auto result =
        motion::PagExporter::Export(*document->document, exportOptions, nullptr);
    if (!result.hasValue()) {
        SetError(errorOut, MessageForError(result.error(), exportOptions.allowBitmapFallback));
        return false;
    }
    return true;
}

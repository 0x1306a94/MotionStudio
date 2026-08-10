#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Document.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxTextTypeface.h"
#include "common/DocumentLock.h"
#include "tgfx/core/Typeface.h"

namespace {

void SetError(char **errorOut, const std::string &message) {
    if (errorOut == nullptr) {
        return;
    }
    *errorOut = strdup(message.c_str());
}

std::string MessageForError(const motion::PagExportError &error) {
    if (!error.message.empty()) {
        return error.message;
    }
    switch (error.kind) {
        case motion::PagExportErrorKind::InvalidComposition:
            return "composition not found";
        case motion::PagExportErrorKind::InvalidOptions:
            return "invalid PAG export options";
        case motion::PagExportErrorKind::MappingFailed:
            return "PAG export mapping failed";
        case motion::PagExportErrorKind::EncodeFailed:
            return "PAG encode failed";
        case motion::PagExportErrorKind::WriteFailed:
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
    exportOptions.allowBitmapExport = options->allowBitmapExport;
    exportOptions.bitmapScale = options->bitmapScale > 0.0f ? options->bitmapScale : 1.0f;
    // Match MS TextLayout baseline (tgfx FontMetrics.ascent) instead of fontSize*0.8.
    exportOptions.textAscentResolver = [](const std::string &fontFamily,
                                          const std::string &fontStyle, float fontSize) -> float {
        std::shared_ptr<tgfx::Typeface> typeface =
            motion::ResolveTextTypeface(fontFamily, fontStyle);
        if (typeface == nullptr || fontSize <= 0.0f) {
            return 0.0f;
        }
        const motion::TgfxGlyphMetrics glyphMetrics(typeface);
        return glyphMetrics.metrics(fontSize).ascent;
    };

    const auto result =
        motion::PagExporter::Export(*document->document, exportOptions, nullptr);
    if (!result.hasValue()) {
        SetError(errorOut, MessageForError(result.error()));
        return false;
    }
    return true;
}

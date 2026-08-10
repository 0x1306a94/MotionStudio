#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagBmpSuffix.h"
#include "MotionStudio/export/PagExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/PrecompContent.h"
#include "TgfxBitmapFrameSource.h"
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

bool ExportTreeHasBmpSuffix(const motion::Document &document, motion::EntityId rootCompositionId) {
    std::unordered_set<uint64_t> visited;
    std::vector<motion::EntityId> stack;
    stack.push_back(rootCompositionId);
    while (!stack.empty()) {
        const motion::EntityId compositionId = stack.back();
        stack.pop_back();
        if (!visited.insert(compositionId.value).second) {
            continue;
        }
        const motion::Composition *composition =
            document.entityIndex().findComposition(compositionId);
        if (composition == nullptr) {
            continue;
        }
        if (motion::HasBmpSuffix(composition->name)) {
            return true;
        }
        for (const auto &layerPtr : composition->layers) {
            if (layerPtr == nullptr) {
                continue;
            }
            if (motion::HasBmpSuffix(layerPtr->name)) {
                return true;
            }
            if (layerPtr->type() != motion::LayerType::Precomp || layerPtr->content == nullptr) {
                continue;
            }
            const auto &content = static_cast<const motion::PrecompContent &>(*layerPtr->content);
            if (content.compositionId.isValid()) {
                stack.push_back(content.compositionId);
            }
        }
    }
    return false;
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
    switch (options->bmpSequenceType) {
        case MS_PAG_BMP_SEQUENCE_VIDEO:
            exportOptions.bmpSequenceType = motion::PagBmpSequenceType::Video;
            break;
        case MS_PAG_BMP_SEQUENCE_BITMAP:
            exportOptions.bmpSequenceType = motion::PagBmpSequenceType::Bitmap;
            break;
        case MS_PAG_BMP_SEQUENCE_AUTO:
        default:
            exportOptions.bmpSequenceType = motion::PagBmpSequenceType::Auto;
            break;
    }
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

    motion::EntityId rootCompositionId = exportOptions.compositionId;
    if (!rootCompositionId.isValid() && !document->document->compositions.empty() &&
        document->document->compositions.front() != nullptr) {
        rootCompositionId = document->document->compositions.front()->id;
    }

    std::unique_ptr<motion::TgfxBitmapFrameSource> bitmapSource;
    if (exportOptions.allowBitmapExport &&
        ExportTreeHasBmpSuffix(*document->document, rootCompositionId)) {
        bitmapSource = std::make_unique<motion::TgfxBitmapFrameSource>();
    }

    const auto result =
        motion::PagExporter::Export(*document->document, exportOptions, bitmapSource.get());
    if (!result.hasValue()) {
        SetError(errorOut, MessageForError(result.error()));
        return false;
    }
    return true;
}

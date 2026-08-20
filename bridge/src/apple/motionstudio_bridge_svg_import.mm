#include "motionstudio_bridge.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "common/DocumentLock.h"
#include "common/MSDocument.h"

namespace {

void SetError(char **error, const std::string &message) {
    if (error == nullptr) {
        return;
    }
    *error = strdup(message.c_str());
}

std::string JsonEscape(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (ch == '\n') {
            out += "\\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

char *DiagnosticsToJson(const std::vector<motion::svg::Diagnostic> &diagnostics) {
    if (diagnostics.empty()) {
        return nullptr;
    }
    std::string json = "[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"code\":\"";
        json += JsonEscape(diagnostics[i].code);
        json += "\",\"message\":\"";
        json += JsonEscape(diagnostics[i].message);
        json += "\",\"nodeName\":\"";
        json += JsonEscape(diagnostics[i].nodeName);
        json += "\"}";
    }
    json += "]";
    return strdup(json.c_str());
}

bool WriteEmbeddedImages(const std::string &projectRoot, const std::vector<motion::svg::EmbeddedImage> &images, std::string &error) {
    if (images.empty()) {
        return true;
    }
    if (projectRoot.empty()) {
        error = "project root is empty";
        return false;
    }
    const std::filesystem::path root(projectRoot);
    std::error_code fsError;
    std::filesystem::create_directories(root / "assets", fsError);
    if (fsError) {
        error = "failed to create assets directory";
        return false;
    }
    for (const motion::svg::EmbeddedImage &image : images) {
        const std::filesystem::path destination = root / image.suggestedFileName;
        std::filesystem::create_directories(destination.parent_path(), fsError);
        std::ofstream output(destination, std::ios::binary);
        if (!output) {
            error = "failed to write embedded image";
            return false;
        }
        output.write(reinterpret_cast<const char *>(image.bytes.data()),
                     static_cast<std::streamsize>(image.bytes.size()));
        if (!output) {
            error = "failed to write embedded image";
            return false;
        }
    }
    return true;
}

}  // namespace

bool ms_document_import_svg(MSDocument *document, uint64_t compositionId, const void *bytes,
                            size_t length, const MSSvgImportOptions *options,
                            MSSvgImportResult *out, char **diagnosticsJson, char **error) {
    DocumentLock lock(document);
    if (document == nullptr) {
        SetError(error, "document is null");
        return false;
    }
    if (bytes == nullptr || length == 0) {
        SetError(error, "svg bytes are empty");
        return false;
    }
    if (out == nullptr) {
        SetError(error, "result is null");
        return false;
    }
    motion::svg::ImportOptions importOptions;
    if (options != nullptr) {
        importOptions.insertIndex = options->insertIndex;
        if (options->parentLayerId != 0) {
            importOptions.parentLayerId = motion::EntityId{options->parentLayerId};
        }
        if (options->rootName != nullptr && options->rootName[0] != '\0') {
            importOptions.rootName = options->rootName;
        }
    }
    auto imported = motion::svg::ImportSvgInto(*document->document, *document->undoManager, motion::EntityId{compositionId}, bytes, length, importOptions);
    if (!imported.hasValue()) {
        SetError(error, imported.error());
        return false;
    }
    std::string writeError;
    if (!WriteEmbeddedImages(document->document->projectRoot, imported.value().embeddedImages, writeError)) {
        document->undoManager->undo(*document->document);
        SetError(error, writeError);
        return false;
    }
    out->rootLayerId = imported.value().rootLayerId.value;
    out->sourceWidth = imported.value().sourceWidth;
    out->sourceHeight = imported.value().sourceHeight;
    if (diagnosticsJson != nullptr) {
        *diagnosticsJson = DiagnosticsToJson(imported.value().diagnostics);
    }
    document->previewSceneCache.clear();
    return true;
}

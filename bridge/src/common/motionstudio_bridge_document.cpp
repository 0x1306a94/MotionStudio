#include "motionstudio_bridge.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/UndoManager.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Composition;
using motion::Document;
using motion::Serializer;
using motion::UndoManager;

MSDocument *ms_document_create(void) {
    auto *handle = new MSDocument();
    handle->document = std::make_unique<Document>();
    handle->undoManager = std::make_unique<UndoManager>();

    auto composition = std::make_unique<Composition>();
    composition->name = "Composition 1";
    composition->duration = 150;  // 5 seconds at the default 30 fps
    handle->document->addComposition(std::move(composition));
    return handle;
}

MSDocument *ms_document_load_json(const char *jsonText, size_t length, char **errorOut) {
    return ms_document_load_json_with_shaders(jsonText, length, nullptr, 0, errorOut);
}

MSDocument *ms_document_load_json_with_shaders(const char *documentJson, size_t documentLength,
                                               const char *shadersJson, size_t shadersLength,
                                               char **errorOut) {
    if (documentJson == nullptr) {
        return nullptr;
    }
    auto result = Serializer::deserialize(std::string(documentJson, documentLength));
    if (!result.hasValue()) {
        if (errorOut != nullptr) {
            *errorOut = strdup(result.error().c_str());
        }
        return nullptr;
    }
    std::unique_ptr<Document> document = std::move(result).value();
    if (shadersJson != nullptr && shadersLength > 0) {
        auto shaders = Serializer::deserializeShaders(std::string(shadersJson, shadersLength));
        if (!shaders.hasValue()) {
            if (errorOut != nullptr) {
                *errorOut = strdup(shaders.error().c_str());
            }
            return nullptr;
        }
        document->shaders = std::move(shaders).value();
    } else {
        document->shaders.clear();
    }
    auto valid = motion::ValidateShaderReferences(*document);
    if (!valid.hasValue()) {
        if (errorOut != nullptr) {
            *errorOut = strdup(valid.error().c_str());
        }
        return nullptr;
    }
    auto *handle = new MSDocument();
    handle->document = std::move(document);
    handle->undoManager = std::make_unique<UndoManager>();
    return handle;
}

MSDocument *ms_document_load(const char *packagePath, char **errorOut) {
    if (packagePath == nullptr || packagePath[0] == '\0') {
        if (errorOut != nullptr) {
            *errorOut = strdup("package path is empty");
        }
        return nullptr;
    }
    const std::filesystem::path root(packagePath);
    const std::filesystem::path documentPath = root / "document.json";
    std::ifstream documentInput(documentPath, std::ios::binary);
    if (!documentInput) {
        if (errorOut != nullptr) {
            *errorOut = strdup("failed to open document.json");
        }
        return nullptr;
    }
    const std::string documentJson((std::istreambuf_iterator<char>(documentInput)),
                                   std::istreambuf_iterator<char>());

    std::string shadersJson;
    const std::filesystem::path shadersPath = root / "shader.json";
    std::ifstream shadersInput(shadersPath, std::ios::binary);
    if (shadersInput) {
        shadersJson.assign((std::istreambuf_iterator<char>(shadersInput)),
                           std::istreambuf_iterator<char>());
    }

    MSDocument *handle = ms_document_load_json_with_shaders(
        documentJson.data(), documentJson.size(),
        shadersJson.empty() ? nullptr : shadersJson.data(), shadersJson.size(), errorOut);
    if (handle == nullptr) {
        return nullptr;
    }
    handle->document->projectRoot = root.lexically_normal().string();
    return handle;
}

void ms_document_set_project_root(MSDocument *document, const char *absolutePath) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return;
    }
    document->document->projectRoot = absolutePath == nullptr ? std::string{} : absolutePath;
}

char *ms_document_project_root(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return nullptr;
    }
    return strdup(document->document->projectRoot.c_str());
}

void ms_document_destroy(MSDocument *document) {
    // No lock: destroying implies unique ownership of the handle.
    delete document;
}

void ms_document_set_content_revision(MSDocument *document, uint64_t revision) {
    if (document == nullptr) {
        return;
    }
    document->contentRevision = revision;
}

uint64_t ms_document_get_content_revision(const MSDocument *document) {
    if (document == nullptr) {
        return 0;
    }
    return document->contentRevision;
}

char *ms_document_save(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return nullptr;
    }
    return strdup(Serializer::serialize(*document->document).c_str());
}

void ms_string_free(char *string) {
    free(string);
}

/* ============================ undo / redo ============================ */

bool ms_document_undo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return false;
    }
    document->undoManager->undo(*document->document);
    document->previewSceneCache.clear();
    return true;
}

bool ms_document_redo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return false;
    }
    document->undoManager->redo(*document->document);
    document->previewSceneCache.clear();
    return true;
}

bool ms_document_can_undo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canUndo();
}

bool ms_document_can_redo(MSDocument *document) {
    DocumentLock guard(document);
    return document != nullptr && document->undoManager->canRedo();
}

char *ms_document_undo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canUndo()) {
        return nullptr;
    }
    return strdup(document->undoManager->undoDescription().c_str());
}

char *ms_document_redo_description(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return nullptr;
    }
    return strdup(document->undoManager->redoDescription().c_str());
}

void ms_document_begin_merge_group(MSDocument *document) {
    DocumentLock guard(document);
    if (document != nullptr) {
        document->undoManager->beginMergeGroup();
    }
}

void ms_document_end_merge_group(MSDocument *document) {
    DocumentLock guard(document);
    if (document != nullptr) {
        document->undoManager->endMergeGroup();
    }
}

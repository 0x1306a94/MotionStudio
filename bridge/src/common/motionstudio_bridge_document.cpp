#include "motionstudio_bridge.h"

#include <cstdlib>
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

MSDocument *ms_document_load(const char *jsonText, size_t length, char **errorOut) {
    if (jsonText == nullptr) {
        return nullptr;
    }
    auto result = Serializer::deserialize(std::string(jsonText, length));
    if (!result.hasValue()) {
        if (errorOut != nullptr) {
            *errorOut = strdup(result.error().c_str());
        }
        return nullptr;
    }
    auto *handle = new MSDocument();
    handle->document = std::move(result).value();
    handle->undoManager = std::make_unique<UndoManager>();
    return handle;
}

void ms_document_destroy(MSDocument *document) {
    // No lock: destroying implies unique ownership of the handle.
    delete document;
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
    return true;
}

bool ms_document_redo(MSDocument *document) {
    DocumentLock guard(document);
    if (document == nullptr || !document->undoManager->canRedo()) {
        return false;
    }
    document->undoManager->redo(*document->document);
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

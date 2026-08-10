#pragma once

#include <string>
#include <utility>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/PagExporter.h"

namespace motion {

inline PagExportError MakePagExportError(PagExportErrorKind kind, EntityId entityId,
                                         std::string entityName, std::string code,
                                         std::string message) {
    PagExportError error;
    error.kind = kind;
    error.entityId = entityId;
    error.entityName = std::move(entityName);
    error.code = std::move(code);
    error.message = std::move(message);
    return error;
}

inline bool IsPagExportCancelled(const volatile int *cancelFlag) {
    return cancelFlag != nullptr && *cancelFlag != 0;
}

inline PagExportError MakeCancelledPagExportError() {
    return MakePagExportError(PagExportErrorKind::EncodeFailed, {}, "", "", "cancelled");
}

}  // namespace motion

//
//  DocumentLock.h
//  MotionStudio
//
//  Created by king on 2026/7/24.
//

#pragma once

#include "MSDocument.h"

// Locks the document's mutex for the duration of one C API call. A null
// handle leaves the lock unheld; callers still null-check afterwards.
struct DocumentLock {
    std::unique_lock<std::mutex> lock;

    explicit DocumentLock(MSDocument *handle) {
        if (handle != nullptr) {
            lock = std::unique_lock<std::mutex>(handle->mutex);
        }
    }
};
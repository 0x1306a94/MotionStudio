#pragma once

// Internal definition of the opaque MSCanvas handle, shared between the
// bridge implementation and the Apple-only canvas sources.

#if defined(__APPLE__)

#include <memory>

#include "TgfxOnScreenAdapter.h"

struct MSCanvas {
    std::unique_ptr<motion::TgfxOnScreenAdapter> adapter;
};

#endif

#pragma once

#include <string>
#include <utility>

namespace motion {

// Error payload for parsing and schema migration. Distinct from std::string
// because Expected<T, E> disallows T == E, and these operations return
// Expected<std::string, ...> results.
struct ParseError {
    // message: human-readable description of the failure.
    explicit ParseError(std::string message)
        : message(std::move(message)) {
    }

    std::string message;
};

}  // namespace motion

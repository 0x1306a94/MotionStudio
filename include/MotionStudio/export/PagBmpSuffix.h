#pragma once

#include <cctype>
#include <string_view>

namespace motion {

inline bool HasBmpSuffix(std::string_view name) {
    static constexpr std::string_view kSuffix = "_bmp";
    if (name.size() < kSuffix.size()) {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - kSuffix.size());
    for (size_t index = 0; index < kSuffix.size(); ++index) {
        const unsigned char left = static_cast<unsigned char>(tail[index]);
        const unsigned char right = static_cast<unsigned char>(kSuffix[index]);
        if (std::tolower(left) != right) {
            return false;
        }
    }
    return true;
}

}  // namespace motion

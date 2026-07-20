#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace motion {

// 失败描述。项目不使用异常，可失败操作经 Expected 传递 Error。
class Error {
public:
    explicit Error(std::string message) : message_(std::move(message)) {}

    const std::string& message() const { return message_; }

private:
    std::string message_;
};

// 参考 std::expected / boost::outcome：持有 T 或 Error。
// 错误态取 value() 会 assert（fail-fast，不抛异常）。
template <typename T>
class Expected {
public:
    Expected(const T& value) : storage_(value) {}
    Expected(T&& value) : storage_(std::move(value)) {}
    Expected(Error error) : storage_(std::move(error)) {}

    bool hasValue() const { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const { return hasValue(); }

    T& value() {
        T* pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected 处于错误态");
        return *pointer;
    }
    const T& value() const {
        const T* pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected 处于错误态");
        return *pointer;
    }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    T valueOr(T fallback) const {
        if (hasValue()) {
            return value();
        }
        return std::move(fallback);
    }

    const std::string& errorMessage() const {
        const Error* error = std::get_if<Error>(&storage_);
        assert(error != nullptr && "Expected 处于值态");
        return error->message();
    }

    static Expected failure(std::string message) {
        return Expected(Error(std::move(message)));
    }

private:
    std::variant<Error, T> storage_;
};

// void 特化：仅表达成功 / 失败。
template <>
class Expected<void> {
public:
    Expected() = default;
    Expected(Error error) : error_(std::move(error)) {}

    bool hasValue() const { return !error_.has_value(); }
    explicit operator bool() const { return hasValue(); }

    const std::string& errorMessage() const {
        assert(error_.has_value() && "Expected 处于值态");
        return error_->message();
    }

    static Expected failure(std::string message) {
        return Expected(Error(std::move(message)));
    }

private:
    std::optional<Error> error_;
};

}  // namespace motion

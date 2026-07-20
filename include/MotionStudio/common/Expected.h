#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace motion {

// Describes a failure. The project does not use exceptions; fallible operations
// propagate errors via Expected<T>.
class Error {
public:
    // message: human-readable description of the failure.
    explicit Error(std::string message) : message_(std::move(message)) {}

    const std::string& message() const { return message_; }

private:
    std::string message_;
};

// Holds either a value of type T or an Error, inspired by std::expected /
// boost::outcome. Calling value() in the error state triggers an assertion
// (fail-fast, no exceptions thrown).
template <typename T>
class Expected {
public:
    Expected(const T& value) : storage_(value) {}
    Expected(T&& value) : storage_(std::move(value)) {}
    Expected(Error error) : storage_(std::move(error)) {}

    // Returns true if the Expected holds a value rather than an error.
    bool hasValue() const { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const { return hasValue(); }

    // Returns the contained value. Asserts if the Expected is in the error state.
    T& value() {
        T* pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected is in error state");
        return *pointer;
    }
    const T& value() const {
        const T* pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected is in error state");
        return *pointer;
    }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    // Returns the contained value, or fallback if the Expected is in the error state.
    // fallback: value to return when no value is present.
    T valueOr(T fallback) const {
        if (hasValue()) {
            return value();
        }
        return std::move(fallback);
    }

    // Returns the error message. Asserts if the Expected is in the value state.
    const std::string& errorMessage() const {
        const Error* error = std::get_if<Error>(&storage_);
        assert(error != nullptr && "Expected is in value state");
        return error->message();
    }

    // Constructs an Expected in the error state.
    // message: description of the failure.
    static Expected Failure(std::string message) {
        return Expected(Error(std::move(message)));
    }

private:
    std::variant<Error, T> storage_;
};

// void specialization: signals success or failure without carrying a value.
template <>
class Expected<void> {
public:
    Expected() = default;
    Expected(Error error) : error_(std::move(error)) {}

    // Returns true if no error is present.
    bool hasValue() const { return !error_.has_value(); }
    explicit operator bool() const { return hasValue(); }

    // Returns the error message. Asserts if no error is present.
    const std::string& errorMessage() const {
        assert(error_.has_value() && "Expected is in value state");
        return error_->message();
    }

    // Constructs an Expected<void> in the error state.
    // message: description of the failure.
    static Expected Failure(std::string message) {
        return Expected(Error(std::move(message)));
    }

private:
    std::optional<Error> error_;
};

}  // namespace motion

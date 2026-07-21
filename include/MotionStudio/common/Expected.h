#pragma once

#include <cassert>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace motion {

// Wraps an error payload so error construction can be distinguished from value
// construction of an Expected. Mirrors std::unexpected.
template <typename E>
class Unexpected {
  public:
    // error: the error payload to carry.
    explicit Unexpected(E error)
        : error_(std::move(error)) {
    }

    E &error() & {
        return error_;
    }
    const E &error() const & {
        return error_;
    }
    E &&error() && {
        return std::move(error_);
    }
    const E &&error() const && {
        return std::move(error_);
    }

  private:
    E error_;
};

template <typename E>
Unexpected(E) -> Unexpected<E>;

template <typename T, typename E>
class Expected;

// Detects Expected specializations, used to constrain the monadic operations.
template <typename T>
struct IsExpected : std::false_type {};

template <typename T, typename E>
struct IsExpected<Expected<T, E>> : std::true_type {};

// Extracts the value type T from an Expected<T, E> specialization.
template <typename T>
struct ExpectedValue {
};

template <typename T, typename E>
struct ExpectedValue<Expected<T, E>> {
    using Type = T;
};

// Holds either a value of type T or an error of type E, mirroring std::expected.
// The error type is arbitrary; most call sites use std::string as the payload.
// Calling value() in the error state (or error() in the value state) triggers an
// assertion: the project does not use exceptions, so misuse fails fast instead.
template <typename T, typename E>
class Expected {
    static_assert(!std::is_same_v<T, E>, "T and E must be different types");

  public:
    // value: payload of the success state.
    Expected(const T &value)
        : storage_(value) {
    }
    Expected(T &&value)
        : storage_(std::move(value)) {
    }

    // unexpected: payload of the failure state.
    Expected(const Unexpected<E> &unexpected)
        : storage_(unexpected.error()) {
    }
    Expected(Unexpected<E> &&unexpected)
        : storage_(std::move(unexpected).error()) {
    }

    // Returns true if the Expected holds a value rather than an error.
    bool hasValue() const {
        return std::holds_alternative<T>(storage_);
    }
    explicit operator bool() const {
        return hasValue();
    }

    // Returns the contained value. Asserts if the Expected is in the error state.
    T &value() & {
        return *checkedValue();
    }
    const T &value() const & {
        return *checkedValue();
    }
    T &&value() && {
        return std::move(*checkedValue());
    }

    // Returns the contained error. Asserts if the Expected is in the value state.
    E &error() & {
        return *checkedError();
    }
    const E &error() const & {
        return *checkedError();
    }
    E &&error() && {
        return std::move(*checkedError());
    }

    T &operator*() & {
        return value();
    }
    const T &operator*() const & {
        return value();
    }
    T *operator->() {
        return &value();
    }
    const T *operator->() const {
        return &value();
    }

    // Returns the contained value, or fallback if the Expected is in the error state.
    // fallback: value to return when no value is present.
    T valueOr(T fallback) const {
        if (hasValue()) {
            return value();
        }
        return std::move(fallback);
    }

    // Invokes f with the contained value if present, otherwise propagates the
    // error. f must return an Expected whose error type is E.
    // f: callable taking the value and returning an Expected<U, E>.
    template <typename F>
    auto andThen(F &&f) & {
        return AndThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) const & {
        return AndThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) && {
        return AndThenImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) const && {
        return AndThenImpl(std::move(*this), std::forward<F>(f));
    }

    // Applies f to the contained value if present, otherwise propagates the error.
    // f: callable taking the value and returning any non-void type U; the result
    // is Expected<U, E> (Expected<void, E> when f returns void).
    template <typename F>
    auto transform(F &&f) & {
        return TransformImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) const & {
        return TransformImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) && {
        return TransformImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) const && {
        return TransformImpl(std::move(*this), std::forward<F>(f));
    }

    // Invokes f with the contained error if present, otherwise propagates the
    // value. f must return an Expected whose value type is T.
    // f: callable taking the error and returning an Expected<T, G>.
    template <typename F>
    auto orElse(F &&f) & {
        return OrElseImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) const & {
        return OrElseImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) && {
        return OrElseImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) const && {
        return OrElseImpl(std::move(*this), std::forward<F>(f));
    }

    // Applies f to the contained error if present, otherwise propagates the value.
    // f: callable taking the error and returning a new error type G; the result
    // is Expected<T, G>.
    template <typename F>
    auto transformError(F &&f) & {
        return TransformErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) const & {
        return TransformErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) && {
        return TransformErrorImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) const && {
        return TransformErrorImpl(std::move(*this), std::forward<F>(f));
    }

  private:
    template <typename Self, typename F>
    static auto AndThenImpl(Self &&self, F &&f) {
        using Result = std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>;
        static_assert(IsExpected<std::decay_t<Result>>::value, "andThen expects f to return an Expected");
        static_assert(!std::is_reference_v<Result>, "andThen expects f to return an Expected by value");
        if (self.hasValue()) {
            return std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
        }
        return Result(Unexpected<E>(std::forward<Self>(self).error()));
    }

    template <typename Self, typename F>
    static auto TransformImpl(Self &&self, F &&f) {
        using U = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>>;
        if (self.hasValue()) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
                return Expected<void, E>();
            } else {
                return Expected<U, E>(std::invoke(std::forward<F>(f), std::forward<Self>(self).value()));
            }
        }
        return Expected<U, E>(Unexpected<E>(std::forward<Self>(self).error()));
    }

    template <typename Self, typename F>
    static auto OrElseImpl(Self &&self, F &&f) {
        using Result = std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>;
        static_assert(IsExpected<std::decay_t<Result>>::value, "orElse expects f to return an Expected");
        static_assert(!std::is_reference_v<Result>, "orElse expects f to return an Expected by value");
        if (self.hasValue()) {
            return Result(std::forward<Self>(self).value());
        }
        return std::invoke(std::forward<F>(f), std::forward<Self>(self).error());
    }

    template <typename Self, typename F>
    static auto TransformErrorImpl(Self &&self, F &&f) {
        using G = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        if (self.hasValue()) {
            return Expected<T, G>(std::forward<Self>(self).value());
        }
        return Expected<T, G>(Unexpected<G>(std::invoke(std::forward<F>(f), std::forward<Self>(self).error())));
    }

    T *checkedValue() {
        T *pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected is in error state");
        return pointer;
    }
    const T *checkedValue() const {
        const T *pointer = std::get_if<T>(&storage_);
        assert(pointer != nullptr && "Expected is in error state");
        return pointer;
    }

    E *checkedError() {
        E *pointer = std::get_if<E>(&storage_);
        assert(pointer != nullptr && "Expected is in value state");
        return pointer;
    }
    const E *checkedError() const {
        const E *pointer = std::get_if<E>(&storage_);
        assert(pointer != nullptr && "Expected is in value state");
        return pointer;
    }

    std::variant<E, T> storage_;
};

// void specialization: signals success or failure without carrying a value.
template <typename E>
class Expected<void, E> {
  public:
    Expected() = default;

    // unexpected: payload of the failure state.
    Expected(const Unexpected<E> &unexpected)
        : error_(unexpected.error()) {
    }
    Expected(Unexpected<E> &&unexpected)
        : error_(std::move(unexpected).error()) {
    }

    // Returns true if no error is present.
    bool hasValue() const {
        return !error_.has_value();
    }
    explicit operator bool() const {
        return hasValue();
    }

    // Returns the contained error. Asserts if no error is present.
    E &error() & {
        assert(error_.has_value() && "Expected is in value state");
        return *error_;
    }
    const E &error() const & {
        assert(error_.has_value() && "Expected is in value state");
        return *error_;
    }
    E &&error() && {
        assert(error_.has_value() && "Expected is in value state");
        return std::move(*error_);
    }

    // Invokes f if no error is present, otherwise propagates the error.
    // f: callable taking no argument and returning an Expected<U, E>.
    template <typename F>
    auto andThen(F &&f) & {
        return AndThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) const & {
        return AndThenImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) && {
        return AndThenImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto andThen(F &&f) const && {
        return AndThenImpl(std::move(*this), std::forward<F>(f));
    }

    // Invokes f if no error is present, otherwise propagates the error.
    // f: callable taking no argument and returning any type U; the result is
    // Expected<U, E> (Expected<void, E> when f returns void).
    template <typename F>
    auto transform(F &&f) & {
        return TransformImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) const & {
        return TransformImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) && {
        return TransformImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto transform(F &&f) const && {
        return TransformImpl(std::move(*this), std::forward<F>(f));
    }

    // Invokes f with the contained error if present, otherwise stays successful.
    // f: callable taking the error and returning an Expected<void, G>; the value
    // type must stay void because a successful Expected<void, E> carries nothing.
    template <typename F>
    auto orElse(F &&f) & {
        return OrElseImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) const & {
        return OrElseImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) && {
        return OrElseImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto orElse(F &&f) const && {
        return OrElseImpl(std::move(*this), std::forward<F>(f));
    }

    // Applies f to the contained error if present, otherwise stays successful.
    // f: callable taking the error and returning a new error type G; the result
    // is Expected<void, G>.
    template <typename F>
    auto transformError(F &&f) & {
        return TransformErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) const & {
        return TransformErrorImpl(*this, std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) && {
        return TransformErrorImpl(std::move(*this), std::forward<F>(f));
    }
    template <typename F>
    auto transformError(F &&f) const && {
        return TransformErrorImpl(std::move(*this), std::forward<F>(f));
    }

  private:
    template <typename Self, typename F>
    static auto AndThenImpl(Self &&self, F &&f) {
        using Result = std::invoke_result_t<F>;
        static_assert(IsExpected<std::decay_t<Result>>::value, "andThen expects f to return an Expected");
        static_assert(!std::is_reference_v<Result>, "andThen expects f to return an Expected by value");
        if (self.hasValue()) {
            return std::invoke(std::forward<F>(f));
        }
        return Result(Unexpected<E>(std::forward<Self>(self).error()));
    }

    template <typename Self, typename F>
    static auto TransformImpl(Self &&self, F &&f) {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (self.hasValue()) {
            if constexpr (std::is_void_v<U>) {
                std::invoke(std::forward<F>(f));
                return Expected<void, E>();
            } else {
                return Expected<U, E>(std::invoke(std::forward<F>(f)));
            }
        }
        return Expected<U, E>(Unexpected<E>(std::forward<Self>(self).error()));
    }

    template <typename Self, typename F>
    static auto OrElseImpl(Self &&self, F &&f) {
        using Result = std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>;
        static_assert(IsExpected<std::decay_t<Result>>::value, "orElse expects f to return an Expected");
        static_assert(!std::is_reference_v<Result>, "orElse expects f to return an Expected by value");
        static_assert(std::is_void_v<typename ExpectedValue<std::decay_t<Result>>::Type>,
                      "orElse on Expected<void, E> expects f to return Expected<void, G>");
        if (self.hasValue()) {
            return Result();
        }
        return std::invoke(std::forward<F>(f), std::forward<Self>(self).error());
    }

    template <typename Self, typename F>
    static auto TransformErrorImpl(Self &&self, F &&f) {
        using G = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).error())>>;
        if (self.hasValue()) {
            return Expected<void, G>();
        }
        return Expected<void, G>(Unexpected<G>(std::invoke(std::forward<F>(f), std::forward<Self>(self).error())));
    }

    std::optional<E> error_;
};

}  // namespace motion

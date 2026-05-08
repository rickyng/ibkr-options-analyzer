#pragma once

#include <expected>
#include <optional>
#include <string>
#include <system_error>

namespace ibkr::utils {

/**
 * Result type for error handling (similar to std::expected in C++23).
 *
 * Provides a type-safe way to return either a value or an error without exceptions.
 * This is the primary error handling mechanism throughout the codebase.
 *
 * Usage:
 *   Result<int> divide(int a, int b) {
 *       if (b == 0) return Error{"Division by zero"};
 *       return a / b;
 *   }
 *
 *   auto result = divide(10, 2);
 *   if (result) {
 *       std::cout << "Result: " << *result << "\n";
 *   } else {
 *       std::cerr << "Error: " << result.error() << "\n";
 *   }
 */

struct Error {
    std::string message;
    std::string context;  // Additional context (e.g., account name, file path)
    int code{0};          // Optional error code

    Error(std::string msg) : message(std::move(msg)) {}

    Error(std::string msg, std::string ctx)
        : message(std::move(msg)), context(std::move(ctx)) {}

    Error(std::string msg, std::string ctx, int error_code)
        : message(std::move(msg)), context(std::move(ctx)), code(error_code) {}

    // Format error for display
    std::string format() const {
        if (context.empty()) {
            return message;
        }
        return message + " [" + context + "]";
    }
};

// Result type using std::expected (C++23) or fallback implementation
#if __cplusplus >= 202302L && __cpp_lib_expected >= 202202L
    // Use standard std::expected if available
    template<typename T>
    using Result = std::expected<T, Error>;
#else
    // Fallback implementation for C++20
    template<typename T>
    class [[nodiscard]] Result {
    public:
        // Constructors
        Result(T value) : value_(std::move(value)), has_value_(true) {}
        Result(Error error) : error_(std::move(error)), has_value_(false) {}

        // Check if result contains a value
        explicit operator bool() const noexcept { return has_value_; }
        bool has_value() const noexcept { return has_value_; }

        // Access value (undefined behavior if !has_value())
        T& operator*() & { return value_; }
        const T& operator*() const & { return value_; }
        T&& operator*() && { return std::move(value_); }

        T* operator->() { return &value_; }
        const T* operator->() const { return &value_; }

        T& value() & {
            if (!has_value_) throw std::runtime_error(error_.format());
            return value_;
        }
        const T& value() const & {
            if (!has_value_) throw std::runtime_error(error_.format());
            return value_;
        }
        T&& value() && {
            if (!has_value_) throw std::runtime_error(error_.format());
            return std::move(value_);
        }

        // Access error (undefined behavior if has_value())
        const Error& error() const & { return error_; }
        Error& error() & { return error_; }
        Error&& error() && { return std::move(error_); }

        // Value or default
        template<typename U>
        T value_or(U&& default_value) const & {
            return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
        }

        template<typename U>
        T value_or(U&& default_value) && {
            return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
        }

    private:
        union {
            T value_;
            Error error_;
        };
        bool has_value_;

    public:
        // Destructor
        ~Result() {
            if (has_value_) {
                value_.~T();
            } else {
                error_.~Error();
            }
        }

        // Copy/move constructors and assignment operators
        Result(const Result& other) : has_value_(other.has_value_) {
            if (has_value_) {
                new (&value_) T(other.value_);
            } else {
                new (&error_) Error(other.error_);
            }
        }

        Result(Result&& other) noexcept : has_value_(other.has_value_) {
            if (has_value_) {
                new (&value_) T(std::move(other.value_));
            } else {
                new (&error_) Error(std::move(other.error_));
            }
        }

        Result& operator=(const Result& other) {
            if (this != &other) {
                this->~Result();
                new (this) Result(other);
            }
            return *this;
        }

        Result& operator=(Result&& other) noexcept {
            if (this != &other) {
                this->~Result();
                new (this) Result(std::move(other));
            }
            return *this;
        }
    };
#endif

// Specialization for void (no value, only success/error)
template<>
class [[nodiscard]] Result<void> {
public:
    Result() : error_{} {}  // success state, no allocation
    Result(Error error) : error_{std::move(error)} {}

    explicit operator bool() const noexcept { return !error_.has_value(); }
    bool has_value() const noexcept { return !error_.has_value(); }

    const Error& error() const & { return *error_; }
    Error& error() & { return *error_; }
    Error&& error() && { return std::move(*error_); }

    void value() const {
        if (error_.has_value()) throw std::runtime_error(error_->format());
    }

private:
    std::optional<Error> error_;  // empty = success, contains Error on failure
};

// Helper function to create error results
template<typename T>
[[nodiscard]] Result<T> make_error(std::string message) {
    return Error{std::move(message)};
}

template<typename T>
[[nodiscard]] Result<T> make_error(std::string message, std::string context) {
    return Error{std::move(message), std::move(context)};
}

template<typename T>
[[nodiscard]] Result<T> make_error(std::string message, std::string context, int code) {
    return Error{std::move(message), std::move(context), code};
}

} // namespace ibkr::utils

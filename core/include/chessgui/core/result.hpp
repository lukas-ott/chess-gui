#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace chessgui::core {

/// Why an operation failed, in words a user can read ("rank 3 has 9 squares").
struct Error {
    std::string message;
};

/// Either a value or an Error. A small stand-in for C++23's std::expected.
///
///     Result<Position> pos = Position::fromFen(text);
///     if (!pos) { show(pos.error().message); return; }
///     use(*pos);
template <typename T> class Result {
public:
    Result(T value) : m_data(std::move(value)) {}     // NOLINT(*-explicit-constructor): implicit on purpose
    Result(Error error) : m_data(std::move(error)) {} // NOLINT(*-explicit-constructor): implicit on purpose

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const& {
        assert(ok());
        return std::get<T>(m_data);
    }
    [[nodiscard]] T& value() & {
        assert(ok());
        return std::get<T>(m_data);
    }
    [[nodiscard]] T&& value() && {
        assert(ok());
        return std::get<T>(std::move(m_data));
    }
    [[nodiscard]] const Error& error() const {
        assert(!ok());
        return std::get<Error>(m_data);
    }

    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }
    T&& operator*() && { return std::move(*this).value(); }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }

private:
    std::variant<T, Error> m_data;
};

} // namespace chessgui::core

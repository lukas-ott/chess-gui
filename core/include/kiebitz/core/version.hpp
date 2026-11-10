#pragma once

#include <string_view>

namespace kiebitz::core {

/// The application version, taken from the CMake project version.
[[nodiscard]] std::string_view version() noexcept;

} // namespace kiebitz::core

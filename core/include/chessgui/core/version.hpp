#pragma once

#include <string_view>

namespace chessgui::core {

/// The application version, taken from the CMake project version.
[[nodiscard]] std::string_view version() noexcept;

} // namespace chessgui::core

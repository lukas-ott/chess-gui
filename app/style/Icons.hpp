#pragma once

#include <QColor>
#include <QIcon>

#include <cstdint>

namespace chessgui::app {

/// Small line icons drawn in code, so they stay crisp at any size and follow the app's colours
/// without shipping image files.
enum class IconKind : std::uint8_t { First, Previous, Next, Last, Close, Folder };

/// An icon with a normal and a disabled look. `size` is in logical pixels.
[[nodiscard]] QIcon makeIcon(IconKind kind, const QColor& color, const QColor& disabledColor,
                             int size = 16);

} // namespace chessgui::app

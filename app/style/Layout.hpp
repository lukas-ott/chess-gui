#pragma once

#include <QMargins>

/// The app's spacing, in one place, so every page and card lines up the same way.
namespace kiebitz::app::layout {

/// Around a whole page (Library, Engines): left, top, right, bottom.
inline constexpr QMargins kPage{20, 16, 20, 20};
/// Between the blocks of a page (header, cards) and between cards side by side.
inline constexpr int kPageSpacing = 14;
/// Inside a card.
inline constexpr QMargins kCard{16, 16, 16, 16};
/// Between the parts of a card (its title, a search field, the list).
inline constexpr int kCardSpacing = 10;
/// Every button, text field, spin box and combo box is this tall (the style sheet agrees).
inline constexpr int kControlHeight = 32;

} // namespace kiebitz::app::layout

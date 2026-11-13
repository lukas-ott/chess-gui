#pragma once

#include <QLatin1StringView>

// The main window's own QSettings keys (MainWindow.cpp and MainWindowSetup.cpp). Compile-time
// constants: nothing to construct at startup.
namespace kiebitz::app::keys {

inline constexpr QLatin1StringView kCustomLight{"board/customLight"};
inline constexpr QLatin1StringView kCustomDark{"board/customDark"};
inline constexpr QLatin1StringView kFlipped{"board/flipped"};
inline constexpr QLatin1StringView kContinuationsLearned{"hints/continuationsLearned"};
inline constexpr QLatin1StringView kLibraryFolder{"library/folder"};

/// The board theme's name when the user picked their own colours.
inline constexpr QLatin1StringView kCustomTheme{"Custom"};
/// How long a status bar message stays.
inline constexpr int kStatusMs = 4000;

} // namespace kiebitz::app::keys

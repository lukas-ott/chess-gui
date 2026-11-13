#pragma once

#include <QColor>
#include <QIcon>

#include <cstdint>

namespace kiebitz::app {

/// Small line icons drawn in code, so they stay crisp at any size and follow the app's colours
/// without shipping image files.
enum class IconKind : std::uint8_t {
    First,
    Previous,
    Next,
    Last,
    Close,
    Folder,
    File,      ///< a page with a folded corner (a PGN file in the Library)
    Branch,    ///< one line splitting in two: a repertoire file
    Trophy,    ///< a cup: a tournament file
    Puzzle,    ///< a puzzle piece: a puzzle file
    Check,     ///< a tick: right
    NewFile,   ///< a page with a plus
    NewFolder, ///< a folder with a plus
    Database,  ///< a stack of discs: game databases (sidebar)
    Person,    ///< a head and shoulders: profiles (sidebar)
    Info,      ///< an "i" in a circle
    Refresh,   ///< a circular arrow: sync
    Calendar,  ///< a page of a calendar: dates
    Trash,     ///< a bin: remove
    Board,     ///< a pawn: the board and its games (sidebar)
    Library,   ///< two stacked pages: the files (sidebar)
    Engine,    ///< a chip (sidebar, engine cards)
    Cloud,     ///< the Lichess cloud
    Plus,
    Gear,      ///< a cog: settings (sidebar)
    Pause,     ///< two bars: stop a clock
    Play,      ///< a triangle: start it again
    Stopwatch, ///< a round watch with a button on top: training (sidebar)
};

/// An icon with a normal and a disabled look. `size` is in logical pixels.
[[nodiscard]] QIcon makeIcon(IconKind kind, const QColor& color, const QColor& disabledColor,
                             int size = 16);

} // namespace kiebitz::app

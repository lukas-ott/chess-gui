#pragma once

#include "kiebitz/repertoire/gaps.hpp"

#include <QString>

namespace kiebitz::app {

/// What the gap check asks how often moves are played: the Explorer's source, with its filter.
struct GapSource {
    QString name;                ///< "Lichess", "Caissabase 2024", ...
    repertoire::Explore explore; ///< empty when it can't be used
    QString unavailable;         ///< why not (Lichess signed out), or empty
};

} // namespace kiebitz::app
